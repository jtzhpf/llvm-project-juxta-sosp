package eg_test

import (
	"bytes"
	"flag"
	"go/parser"
	"go/token"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"llvm.org/llgo/third_party/go.tools/go/exact"
	"llvm.org/llgo/third_party/go.tools/go/loader"
	"llvm.org/llgo/third_party/go.tools/go/types"
	"llvm.org/llgo/third_party/go.tools/refactor/eg"
)

// TODO(adonovan): more tests:
// - of command-line tool
// - of all parts of syntax
// - of applying a template to a package it imports:
//   the replacement syntax should use unqualified names for its objects.

var (
	updateFlag  = flag.Bool("update", false, "update the golden files")
	verboseFlag = flag.Bool("verbose", false, "show matcher information")
)

func Test(t *testing.T) {
	switch runtime.GOOS {
	case "windows":
		t.Skipf("skipping test on %q (no /usr/bin/diff)", runtime.GOOS)
	}

	conf := loader.Config{
		Fset:          token.NewFileSet(),
		ParserMode:    parser.ParseComments,
		SourceImports: true,
	}

	// Each entry is a single-file package.
	// (Multi-file packages aren't interesting for this test.)
	// Order matters: each non-template package is processed using
	// the preceding template package.
	for _, filename := range []string{
		"testdata/A.template",
		"testdata/A1.go",
		"testdata/A2.go",

		"testdata/B.template",
		"testdata/B1.go",

		"testdata/C.template",
		"testdata/C1.go",

		"testdata/D.template",
		"testdata/D1.go",

		"testdata/E.template",
		"testdata/E1.go",

		"testdata/F.template",
		"testdata/F1.go",

		"testdata/bad_type.template",
		"testdata/no_before.template",
		"testdata/no_after_return.template",
		"testdata/type_mismatch.template",
		"testdata/expr_type_mismatch.template",
	} {
		pkgname := strings.TrimSuffix(filepath.Base(filename), ".go")
		if err := conf.CreateFromFilenames(pkgname, filename); err != nil {
			t.Fatal(err)
		}
	}
	iprog, err := conf.Load()
	if err != nil {
		t.Fatal(err)
	}

	var xform *eg.Transformer
	for _, info := range iprog.Created {
		file := info.Files[0]
		filename := iprog.Fset.File(file.Pos()).Name() // foo.go

		if strings.HasSuffix(filename, "template") {
			// a new template
			shouldFail, _ := info.Pkg.Scope().Lookup("shouldFail").(*types.Const)
			xform, err = eg.NewTransformer(iprog.Fset, info, *verboseFlag)
			if err != nil {
				if shouldFail == nil {
					t.Errorf("NewTransformer(%s): %s", filename, err)
				} else if want := exact.StringVal(shouldFail.Val()); !strings.Contains(err.Error(), want) {
					t.Errorf("NewTransformer(%s): got error %q, want error %q", filename, err, want)
				}
			} else if shouldFail != nil {
				t.Errorf("NewTransformer(%s) succeeded unexpectedly; want error %q",
					filename, shouldFail.Val())
			}
			continue
		}

		if xform == nil {
			t.Errorf("%s: no previous template", filename)
			continue
		}

		// apply previous template to this package
		n := xform.Transform(&info.Info, info.Pkg, file)
		if n == 0 {
			t.Errorf("%s: no matches", filename)
			continue
		}

		got := filename + "t"       // foo.got
		golden := filename + "lden" // foo.golden

		// Write actual output to foo.got.
		if err := eg.WriteAST(iprog.Fset, got, file); err != nil {
			t.Error(err)
		}
		defer os.Remove(got)

		// Compare foo.got with foo.golden.
		var cmd *exec.Cmd
		switch runtime.GOOS {
		case "plan9":
			cmd = exec.Command("/bin/diff", "-c", golden, got)
		default:
			cmd = exec.Command("/usr/bin/diff", "-u", golden, got)
		}
		buf := new(bytes.Buffer)
		cmd.Stdout = buf
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			t.Errorf("eg tests for %s failed: %s.\n%s\n", filename, err, buf)

			if *updateFlag {
				t.Logf("Updating %s...", golden)
				if err := exec.Command("/bin/cp", got, golden).Run(); err != nil {
					t.Errorf("Update failed: %s", err)
				}
			}
		}
	}
}
