// Copyright 2014 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// callgraph: a tool for reporting the call graph of a Go program.
// See Usage for details, or run with -help.
package main

// TODO(adonovan):
//
// Features:
// - restrict graph to a single package
// - output
//   - functions reachable from root (use digraph tool?)
//   - unreachable functions (use digraph tool?)
//   - dynamic (runtime) types
//   - indexed output (numbered nodes)
//   - JSON output
//   - additional template fields:
//     callee file/line/col

import (
	"bytes"
	"flag"
	"fmt"
	"go/build"
	"go/token"
	"io"
	"os"
	"runtime"
	"text/template"

	"llvm.org/llgo/third_party/go.tools/go/callgraph"
	"llvm.org/llgo/third_party/go.tools/go/callgraph/rta"
	"llvm.org/llgo/third_party/go.tools/go/loader"
	"llvm.org/llgo/third_party/go.tools/go/pointer"
	"llvm.org/llgo/third_party/go.tools/go/ssa"
)

var algoFlag = flag.String("algo", "rta",
	`Call graph construction algorithm, one of "rta" or "pta"`)

var testFlag = flag.Bool("test", false,
	"Loads test code (*_test.go) for imported packages")

var formatFlag = flag.String("format",
	"{{.Caller}}\t--{{.Dynamic}}-{{.Line}}:{{.Column}}-->\t{{.Callee}}",
	"A template expression specifying how to format an edge")

const Usage = `callgraph: display the the call graph of a Go program.

Usage:

  callgraph [-algo=rta|pta] [-test] [-format=...] <args>...

Flags:

-algo      Specifies the call-graph construction algorithm.  One of:
           "rta": Rapid Type Analysis  (simple and fast)
           "pta": inclusion-based Points-To Analysis (slower but more precise)

-test      Include the package's tests in the analysis.

-format    Specifies the format in which each call graph edge is displayed.
           One of:
           "digraph":   output suitable for input to
                        golang.org/x/tools/cmd/digraph.
           "graphviz":  output in AT&T GraphViz (.dot) format.

           All other values are interpreted using text/template syntax.
           The default value is:

            {{.Caller}}\t--{{.Dynamic}}-{{.Line}}:{{.Column}}-->\t{{.Callee}}

           The structure passed to the template is (effectively):

                   type Edge struct {
                           Caller      *ssa.Function // calling function
                           Callee      *ssa.Function // called function

                           // Call site:
                           Filename    string // containing file
                           Offset      int    // offset within file of '('
                           Line        int    // line number
                           Column      int    // column number of call
                           Dynamic     string // "static" or "dynamic"
                           Description string // e.g. "static method call"
                   }

           Caller and Callee are *ssa.Function values, which print as
           "(*sync/atomic.Mutex).Lock", but other attributes may be
           derived from them, e.g. Caller.Pkg.Object.Path yields the
           import path of the enclosing package.  Consult the go/ssa
           API documentation for details.

` + loader.FromArgsUsage + `

Examples:

  Show the call graph of the trivial web server application:

    callgraph -format digraph $GOROOT/src/net/http/triv.go

  Same, but show only the packages of each function:

    callgraph -format '{{.Caller.Pkg.Object.Path}} -> {{.Callee.Pkg.Object.Path}}' \
      $GOROOT/src/net/http/triv.go | sort | uniq

  Show functions that make dynamic calls into the 'fmt' test package,
  using the pointer analysis algorithm:

    callgraph -format='{{.Caller}} -{{.Dynamic}}-> {{.Callee}}' -test -algo=pta fmt |
      sed -ne 's/-dynamic-/--/p' |
      sed -ne 's/-->.*fmt_test.*$//p' | sort | uniq

  Show all functions directly called by the callgraph tool's main function:

    callgraph -format=digraph golang.org/x/tools/cmd/callgraph |
      digraph succs golang.org/x/tools/cmd/callgraph.main
`

func init() {
	// If $GOMAXPROCS isn't set, use the full capacity of the machine.
	// For small machines, use at least 4 threads.
	if os.Getenv("GOMAXPROCS") == "" {
		n := runtime.NumCPU()
		if n < 4 {
			n = 4
		}
		runtime.GOMAXPROCS(n)
	}
}

func main() {
	flag.Parse()
	if err := doCallgraph(&build.Default, *algoFlag, *formatFlag, *testFlag, flag.Args()); err != nil {
		fmt.Fprintf(os.Stderr, "callgraph: %s\n", err)
		os.Exit(1)
	}
}

var stdout io.Writer = os.Stdout

func doCallgraph(ctxt *build.Context, algo, format string, tests bool, args []string) error {
	conf := loader.Config{
		Build:         ctxt,
		SourceImports: true,
	}

	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, Usage)
		return nil
	}

	// Use the initial packages from the command line.
	args, err := conf.FromArgs(args, tests)
	if err != nil {
		return err
	}

	// Load, parse and type-check the whole program.
	iprog, err := conf.Load()
	if err != nil {
		return err
	}

	// Create and build SSA-form program representation.
	prog := ssa.Create(iprog, 0)
	prog.BuildAll()

	// Determine the main package.
	// TODO(adonovan): allow independent control over tests, mains
	// and libraries.
	// TODO(adonovan): put this logic in a library; we keep reinventing it.
	var main *ssa.Package
	pkgs := prog.AllPackages()
	if tests {
		// If -test, use all packages' tests.
		if len(pkgs) > 0 {
			main = prog.CreateTestMainPackage(pkgs...)
		}
		if main == nil {
			return fmt.Errorf("no tests")
		}
	} else {
		// Otherwise, use main.main.
		for _, pkg := range pkgs {
			if pkg.Object.Name() == "main" {
				main = pkg
				if main.Func("main") == nil {
					return fmt.Errorf("no func main() in main package")
				}
				break
			}
		}
		if main == nil {
			return fmt.Errorf("no main package")
		}
	}

	// Invariant: main package has a main() function.

	// -- call graph construction ------------------------------------------

	var cg *callgraph.Graph

	switch algo {
	case "pta":
		config := &pointer.Config{
			Mains:          []*ssa.Package{main},
			BuildCallGraph: true,
		}
		ptares, err := pointer.Analyze(config)
		if err != nil {
			return err // internal error in pointer analysis
		}
		cg = ptares.CallGraph

	case "rta":
		roots := []*ssa.Function{
			main.Func("init"),
			main.Func("main"),
		}
		rtares := rta.Analyze(roots, true)
		cg = rtares.CallGraph

		// NB: RTA gives us Reachable and RuntimeTypes too.

	default:
		return fmt.Errorf("unknown algorithm: %s", algo)
	}

	cg.DeleteSyntheticNodes()

	// -- output------------------------------------------------------------

	var before, after string

	// Pre-canned formats.
	switch format {
	case "digraph":
		format = `{{printf "%q %q" .Caller .Callee}}`

	case "graphviz":
		before = "digraph callgraph {\n"
		after = "}\n"
		format = `  {{printf "%q" .Caller}} -> {{printf "%q" .Callee}}"`
	}

	tmpl, err := template.New("-format").Parse(format)
	if err != nil {
		return fmt.Errorf("invalid -format template: %v", err)
	}

	// Allocate these once, outside the traversal.
	var buf bytes.Buffer
	data := Edge{fset: prog.Fset}

	fmt.Fprint(stdout, before)
	if err := callgraph.GraphVisitEdges(cg, func(edge *callgraph.Edge) error {
		data.position.Offset = -1
		data.edge = edge
		data.Caller = edge.Caller.Func
		data.Callee = edge.Callee.Func

		buf.Reset()
		if err := tmpl.Execute(&buf, &data); err != nil {
			return err
		}
		stdout.Write(buf.Bytes())
		if len := buf.Len(); len == 0 || buf.Bytes()[len-1] != '\n' {
			fmt.Fprintln(stdout)
		}
		return nil
	}); err != nil {
		return err
	}
	fmt.Fprint(stdout, after)
	return nil
}

type Edge struct {
	Caller *ssa.Function
	Callee *ssa.Function

	edge     *callgraph.Edge
	fset     *token.FileSet
	position token.Position // initialized lazily
}

func (e *Edge) pos() *token.Position {
	if e.position.Offset == -1 {
		e.position = e.fset.Position(e.edge.Pos()) // called lazily
	}
	return &e.position
}

func (e *Edge) Filename() string { return e.pos().Filename }
func (e *Edge) Column() int      { return e.pos().Column }
func (e *Edge) Line() int        { return e.pos().Line }
func (e *Edge) Offset() int      { return e.pos().Offset }

func (e *Edge) Dynamic() string {
	if e.edge.Site != nil && e.edge.Site.Common().StaticCallee() == nil {
		return "dynamic"
	}
	return "static"
}

func (e *Edge) Description() string { return e.edge.Description() }
