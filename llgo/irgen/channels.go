//===- channels.go - IR generation for channels ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements IR generation for channels.
//
//===----------------------------------------------------------------------===//

package irgen

import (
	"llvm.org/llgo/third_party/go.tools/go/types"
	"llvm.org/llvm/bindings/go/llvm"
)

// makeChan implements make(chantype[, size])
func (fr *frame) makeChan(chantyp types.Type, size *govalue) *govalue {
	// TODO(pcc): call __go_new_channel_big here if needed
	dyntyp := fr.types.ToRuntime(chantyp)
	size = fr.convert(size, types.Typ[types.Uintptr])
	ch := fr.runtime.newChannel.call(fr, dyntyp, size.value)[0]
	return newValue(ch, chantyp)
}

// chanSend implements ch<- x
func (fr *frame) chanSend(ch *govalue, elem *govalue) {
	elemtyp := ch.Type().Underlying().(*types.Chan).Elem()
	elem = fr.convert(elem, elemtyp)
	elemptr := fr.allocaBuilder.CreateAlloca(elem.value.Type(), "")
	fr.builder.CreateStore(elem.value, elemptr)
	elemptr = fr.builder.CreateBitCast(elemptr, llvm.PointerType(llvm.Int8Type(), 0), "")
	chantyp := fr.types.ToRuntime(ch.Type())
	fr.runtime.sendBig.call(fr, chantyp, ch.value, elemptr)
}

// chanRecv implements x[, ok] = <-ch
func (fr *frame) chanRecv(ch *govalue, commaOk bool) (x, ok *govalue) {
	elemtyp := ch.Type().Underlying().(*types.Chan).Elem()
	ptr := fr.allocaBuilder.CreateAlloca(fr.types.ToLLVM(elemtyp), "")
	ptri8 := fr.builder.CreateBitCast(ptr, llvm.PointerType(llvm.Int8Type(), 0), "")
	chantyp := fr.types.ToRuntime(ch.Type())

	if commaOk {
		okval := fr.runtime.chanrecv2.call(fr, chantyp, ch.value, ptri8)[0]
		ok = newValue(okval, types.Typ[types.Bool])
	} else {
		fr.runtime.receive.call(fr, chantyp, ch.value, ptri8)
	}
	x = newValue(fr.builder.CreateLoad(ptr, ""), elemtyp)
	return
}

// chanClose implements close(ch)
func (fr *frame) chanClose(ch *govalue) {
	fr.runtime.builtinClose.call(fr, ch.value)
}

// selectState is equivalent to ssa.SelectState
type selectState struct {
	Dir  types.ChanDir
	Chan *govalue
	Send *govalue
}

func (fr *frame) chanSelect(states []selectState, blocking bool) (index, recvOk *govalue, recvElems []*govalue) {
	n := uint64(len(states))
	if !blocking {
		// non-blocking means there's a default case
		n++
	}
	size := llvm.ConstInt(llvm.Int32Type(), n, false)
	selectp := fr.runtime.newSelect.call(fr, size)[0]

	// Allocate stack for the values to send and receive.
	//
	// TODO(axw) check if received elements have any users, and
	// elide stack allocation if not (pass nil to recv2 instead.)
	ptrs := make([]llvm.Value, len(states))
	for i, state := range states {
		chantyp := state.Chan.Type().Underlying().(*types.Chan)
		elemtyp := fr.types.ToLLVM(chantyp.Elem())
		ptrs[i] = fr.allocaBuilder.CreateAlloca(elemtyp, "")
		if state.Dir == types.SendOnly {
			fr.builder.CreateStore(state.Send.value, ptrs[i])
		} else {
			recvElems = append(recvElems, newValue(ptrs[i], chantyp.Elem()))
		}
	}

	// Create select{send,recv2} calls.
	var receivedp llvm.Value
	if len(recvElems) > 0 {
		receivedp = fr.allocaBuilder.CreateAlloca(fr.types.ToLLVM(types.Typ[types.Bool]), "")
	}
	if !blocking {
		// If the default case is chosen, the index must be -1.
		fr.runtime.selectdefault.call(fr, selectp, llvm.ConstAllOnes(llvm.Int32Type()))
	}
	for i, state := range states {
		ch := state.Chan.value
		index := llvm.ConstInt(llvm.Int32Type(), uint64(i), false)
		if state.Dir == types.SendOnly {
			fr.runtime.selectsend.call(fr, selectp, ch, ptrs[i], index)
		} else {
			fr.runtime.selectrecv2.call(fr, selectp, ch, ptrs[i], receivedp, index)
		}
	}

	// Fire off the select.
	index = newValue(fr.runtime.selectgo.call(fr, selectp)[0], types.Typ[types.Int])
	if len(recvElems) > 0 {
		recvOk = newValue(fr.builder.CreateLoad(receivedp, ""), types.Typ[types.Bool])
		for _, recvElem := range recvElems {
			recvElem.value = fr.builder.CreateLoad(recvElem.value, "")
		}
	}
	return index, recvOk, recvElems
}
