"""x86-64 machine backend.

Pipeline: parse → type check → MIR → machine instructions → bytes → ELF64.

CForge registers r0-r7 are a fixed map onto the System V argument registers.
The return value is r0, copied to rax by `ret`. This file does not call gcc.
"""

import importlib.machinery
import importlib.util
import os
import stat
import struct


def _frontend():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cforge")
    loader = importlib.machinery.SourceFileLoader("cforge_frontend", path)
    spec = importlib.util.spec_from_loader("cforge_frontend", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


_FE = _frontend()
CompileError = _FE.CompileError
Parser = _FE.Parser
lex = _FE.lex

# CForge register → x86-64 register number (rax=0 … rdi=7, r8=8 …).
HW = {
    0: 7,   # r0 rdi
    1: 6,   # r1 rsi
    2: 2,   # r2 rdx
    3: 1,   # r3 rcx
    4: 8,   # r4 r8
    5: 9,   # r5 r9
    6: 10,  # r6 r10
    7: 11,  # r7 r11
}
HW_NAME = {
    7: "rdi",
    6: "rsi",
    2: "rdx",
    1: "rcx",
    8: "r8",
    9: "r9",
    10: "r10",
    11: "r11",
    0: "rax",
}


def rex(w, r, b):
    return 0x40 | (w << 3) | (r << 2) | b


def modrm(reg, rm):
    return 0xC0 | ((reg & 7) << 3) | (rm & 7)


class Asm:
    def __init__(self):
        self.buf = bytearray()
        self.listing = []
        self.fixups = []

    def mark(self, text, data):
        self.listing.append((len(self.buf), text))
        self.buf += bytes(data)

    def rr(self, opcode, dst_hw, src_hw, text):
        self.mark(
            text,
            bytes(
                [
                    rex(1, 1 if src_hw >= 8 else 0, 1 if dst_hw >= 8 else 0),
                    opcode,
                    modrm(src_hw, dst_hw),
                ]
            ),
        )

    def mov_rr(self, dst_hw, src_hw):
        self.rr(0x89, dst_hw, src_hw, "mov %s, %s" % (HW_NAME[dst_hw], HW_NAME[src_hw]))

    def add_rr(self, dst_hw, src_hw):
        self.rr(0x01, dst_hw, src_hw, "add %s, %s" % (HW_NAME[dst_hw], HW_NAME[src_hw]))

    def sub_rr(self, dst_hw, src_hw):
        self.rr(0x29, dst_hw, src_hw, "sub %s, %s" % (HW_NAME[dst_hw], HW_NAME[src_hw]))

    def mov_imm(self, dst_hw, imm):
        if imm < -0x80000000 or imm > 0x7FFFFFFF:
            raise CompileError("immediate does not fit in 32 bits: %s" % imm)
        self.mark(
            "mov %s, %d" % (HW_NAME[dst_hw], imm),
            bytes(
                [
                    rex(1, 0, 1 if dst_hw >= 8 else 0),
                    0xC7,
                    modrm(0, dst_hw),
                ]
            )
            + int(imm).to_bytes(4, "little", signed=True),
        )

    def ret_r0(self):
        # Value lives in r0 (rdi). System V returns in rax.
        self.mov_rr(0, HW[0])
        self.mark("ret", b"\xc3")

    def call(self, name):
        self.mark("call %s" % name, b"\xe8\x00\x00\x00\x00")
        self.fixups.append((len(self.buf) - 4, name))

    def syscall(self):
        self.mark("syscall", b"\x0f\x05")

    def patch(self, symbols):
        for at, name in self.fixups:
            if name not in symbols:
                raise CompileError("unknown function %s" % name)
            rel = symbols[name] - (at + 4)
            self.buf[at : at + 4] = rel.to_bytes(4, "little", signed=True)


def integer_type(typ, line):
    if typ["stars"] or typ["name"] not in ("u64", "u32", "u16", "u8", "i64", "i32"):
        raise CompileError("%s: machine backend supports integer parameters and returns" % line)


def check_fn(fn):
    integer_type(fn["ret"], fn["line"])
    if len(fn["params"]) > 6:
        raise CompileError("%s: at most 6 parameters" % fn["line"])
    for param in fn["params"]:
        integer_type(param["type"], fn["line"])
    saw_exit = False
    for stmt in fn["body"]:
        if stmt["op"] in ("let", "if", "while"):
            raise CompileError(
                "%s: machine slice has raw instructions, return, and calls; %s is not lowered yet"
                % (stmt["line"], stmt["op"])
            )
        if stmt["op"] == "return" or (stmt["op"] == "insn" and stmt["mnem"] == "ret"):
            saw_exit = True
        if stmt["op"] == "expr":
            raise CompileError("%s: bare calls are not a machine statement; use return" % stmt["line"])
    if not saw_exit:
        raise CompileError("%s: %s does not return" % (fn["line"], fn["name"]))


def param_reg(fn, name):
    for i, param in enumerate(fn["params"]):
        if param["name"] == name:
            return i
    raise CompileError("unknown name %s" % name)


def lower_return(asm, fn, expr):
    op = expr["op"]
    if op == "bin" and expr["bin"] in ("+", "-"):
        left, right = expr["left"], expr["right"]
        if left["op"] != "ident" or right["op"] != "ident":
            raise CompileError("machine return supports ident + ident or a call")
        lr = param_reg(fn, left["value"])
        rr = param_reg(fn, right["value"])
        if rr == 0 and lr != 0:
            asm.mov_rr(HW[7], HW[0])
            asm.mov_rr(HW[0], HW[lr])
            rr = 7
        elif lr != 0:
            asm.mov_rr(HW[0], HW[lr])
        if expr["bin"] == "+":
            asm.add_rr(HW[0], HW[rr])
        else:
            asm.sub_rr(HW[0], HW[rr])
        asm.ret_r0()
        return
    if op == "call":
        if expr["fn"]["op"] != "ident":
            raise CompileError("call target must be a name")
        for i, arg in enumerate(expr["args"]):
            if arg["op"] != "num":
                raise CompileError("machine calls take integer literals")
            asm.mov_imm(HW[i], int(arg["value"]))
        asm.call(expr["fn"]["value"])
        asm.mark("ret", b"\xc3")
        return
    if op == "ident":
        reg = param_reg(fn, expr["value"])
        if reg != 0:
            asm.mov_rr(HW[0], HW[reg])
        asm.ret_r0()
        return
    if op == "num":
        asm.mov_imm(HW[0], int(expr["value"]))
        asm.ret_r0()
        return
    raise CompileError("machine return supports integer math, a name, or a call")


def lower_insn(asm, stmt):
    mnem = stmt["mnem"]
    args = stmt["args"]
    if mnem == "ret":
        asm.ret_r0()
        return
    if mnem == "syscall":
        asm.syscall()
        return
    if len(args) != 2 or args[0]["kind"] != "reg":
        raise CompileError("%s: %s rD, rS|imm" % (stmt["line"], mnem))
    dst = HW[args[0]["value"]]
    if args[1]["kind"] == "imm":
        if mnem != "mov":
            raise CompileError("%s: immediate source is only valid for mov" % stmt["line"])
        asm.mov_imm(dst, args[1]["value"])
        return
    src = HW[args[1]["value"]]
    if mnem == "mov":
        asm.mov_rr(dst, src)
    elif mnem == "add":
        asm.add_rr(dst, src)
    elif mnem == "sub":
        asm.sub_rr(dst, src)
    else:
        raise CompileError("unknown instruction %s" % mnem)


def lower_fn(asm, fn, symbols):
    symbols[fn["name"]] = len(asm.buf)
    asm.listing.append((len(asm.buf), "%s:" % fn["name"]))
    for stmt in fn["body"]:
        if stmt["op"] == "insn":
            lower_insn(asm, stmt)
        elif stmt["op"] == "return":
            lower_return(asm, fn, stmt["expr"])
        else:
            raise CompileError("cannot lower %s" % stmt["op"])


def emit_start(asm):
    asm.listing.append((len(asm.buf), "_start:"))
    asm.call("main")
    asm.mov_rr(7, 0)  # mov rdi, rax
    asm.mark("mov eax, 60", b"\xb8\x3c\x00\x00\x00")
    asm.syscall()


def elf64(code, entry_off):
    code_off = 128
    load = 0x400000
    entry = load + code_off + entry_off
    filesz = code_off + len(code)
    ehdr = bytearray(64)
    ehdr[0:4] = b"\x7fELF"
    ehdr[4] = 2
    ehdr[5] = 1
    ehdr[6] = 1
    struct.pack_into(
        "<HHIQQQIHHHHHH",
        ehdr,
        16,
        2,
        0x3E,
        1,
        entry,
        64,
        0,
        0,
        64,
        56,
        1,
        0,
        0,
        0,
    )
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,
        5,
        0,
        load,
        load,
        filesz,
        filesz,
        0x1000,
    )
    blob = bytearray(code_off) + code
    blob[0:64] = ehdr
    blob[64:120] = phdr
    return bytes(blob)


def compile_machine(src_path, out_path):
    with open(src_path, encoding="utf-8") as f:
        src = f.read()
    fns = Parser(lex(src)).parse()
    if not any(fn["name"] == "main" for fn in fns):
        raise CompileError("machine program needs main")
    for fn in fns:
        check_fn(fn)
    asm = Asm()
    symbols = {}
    for fn in fns:
        lower_fn(asm, fn, symbols)
    entry_off = len(asm.buf)
    emit_start(asm)
    asm.patch(symbols)
    image = elf64(asm.buf, entry_off)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(image)
    os.chmod(out_path, os.stat(out_path).st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    listing = []
    for off, text in asm.listing:
        if text.endswith(":"):
            listing.append(text)
        else:
            listing.append("  %04x  %s" % (off, text))
    return out_path, listing
