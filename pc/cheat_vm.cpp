#include "cheat_vm.hpp"

namespace {

uint64_t maskWidth(uint64_t v, uint32_t width) {
    switch (width) {
        case 1: return (uint8_t)v;
        case 2: return (uint16_t)v;
        case 4: return (uint32_t)v;
        case 8: return v;
    }
    return v;
}

bool validWidth(uint32_t w) { return w == 1 || w == 2 || w == 4 || w == 8; }

bool compare(uint64_t a, uint32_t cond, uint64_t b) {
    switch (cond) {
        case 1: return a > b;
        case 2: return a >= b;
        case 3: return a < b;
        case 4: return a <= b;
        case 5: return a == b;
        case 6: return a != b;
    }
    return false;
}

}  // namespace

uint64_t CheatVmEmu::address(const vnx::ProcessInfo& pi, uint32_t memType, uint64_t rel) {
    switch (memType) {
        case 1: return pi.heap.base + rel;
        case 2: return pi.alias.base + rel;
        case 3: return pi.aslr.base + rel;
        case 4: return rel;
        default: return pi.main.base + rel;
    }
}

bool CheatVmEmu::decode(Op& op) {
    bool valid = true;
    auto next = [&]() -> uint32_t {
        if (m_ip >= m_prog->size()) {
            valid = false;
            return 0;
        }
        return (*m_prog)[m_ip++];
    };
    // GetNextVmInt: primero la palabra alta (el orden está garantizado).
    auto nextVmInt = [&](uint32_t width) -> uint64_t {
        const uint32_t first = next();
        switch (width) {
            case 1: return (uint8_t)first;
            case 2: return (uint16_t)first;
            case 4: return first;
            case 8: {
                const uint32_t second = next();
                return ((uint64_t)first << 32) | second;
            }
        }
        return 0;
    };

    const uint32_t first = next();
    if (!valid) return false;
    uint32_t type = (first >> 28) & 0xF;
    if (type >= 0xC) type = (type << 4) | ((first >> 24) & 0xF);
    if (type >= 0xF0) type = (type << 4) | ((first >> 20) & 0xF);

    op = Op();
    op.type = type;
    op.beginCond = type == 0x1 || type == 0x8 || type == 0xC0;
    switch (type) {
        case 0x0: {   // 0TMR00AA AAAAAAAA VVVVVVVV (VVVVVVVV)
            const uint32_t second = next();
            op.width = (first >> 24) & 0xF;
            op.memType = (first >> 20) & 0xF;
            op.reg = (first >> 16) & 0xF;
            op.rel = ((uint64_t)(first & 0xFF) << 32) | second;
            op.value = nextVmInt(op.width);
            break;
        }
        case 0x1: {   // 1TMC00AA AAAAAAAA VVVVVVVV (VVVVVVVV)
            const uint32_t second = next();
            op.width = (first >> 24) & 0xF;
            op.memType = (first >> 20) & 0xF;
            op.cond = (first >> 16) & 0xF;
            op.rel = ((uint64_t)(first & 0xFF) << 32) | second;
            op.value = nextVmInt(op.width);
            break;
        }
        case 0x2:   // 2X000000
            op.isElse = ((first >> 24) & 0xF) == 1;
            break;
        case 0x4: {   // 400R0000 VVVVVVVV VVVVVVVV
            op.reg = (first >> 16) & 0xF;
            const uint32_t hi = next();
            const uint32_t lo = next();
            op.value = ((uint64_t)hi << 32) | lo;
            break;
        }
        case 0x5: {   // 5TMRI0AA AAAAAAAA
            const uint32_t second = next();
            op.width = (first >> 24) & 0xF;
            op.memType = (first >> 20) & 0xF;
            op.reg = (first >> 16) & 0xF;
            op.loadMode = (first >> 12) & 0xF;
            op.reg2 = (first >> 8) & 0xF;
            op.rel = ((uint64_t)(first & 0xFF) << 32) | second;
            break;
        }
        case 0x9:   // 9TCRSIs0 (VVVVVVVV (VVVVVVVV))
            op.width = (first >> 24) & 0xF;
            op.math = (first >> 20) & 0xF;
            op.reg = (first >> 16) & 0xF;
            op.reg2 = (first >> 12) & 0xF;
            op.hasImm = ((first >> 8) & 0xF) != 0;
            if (op.hasImm)
                op.value = nextVmInt(op.width);
            else
                op.reg3 = (first >> 4) & 0xF;
            break;
        case 0xA:   // ATSRIOxa (aaaaaaaa)
            op.width = (first >> 24) & 0xF;
            op.reg = (first >> 20) & 0xF;    // registro a guardar
            op.reg2 = (first >> 16) & 0xF;   // registro con la dirección
            op.incr = ((first >> 12) & 0xF) != 0;
            op.ofsType = (first >> 8) & 0xF;
            op.reg3 = (first >> 4) & 0xF;
            switch (op.ofsType) {
                case 0:
                case 1: break;
                case 2: op.rel = ((uint64_t)(first & 0xF) << 32) | next(); break;
                case 3: op.memType = (first >> 4) & 0xF; break;
                case 4:
                case 5:
                    op.memType = (first >> 4) & 0xF;
                    op.rel = ((uint64_t)(first & 0xF) << 32) | next();
                    break;
                default: op.ofsType = 0; break;
            }
            break;
        case 0xC0:   // C0TcSX##
            op.width = (first >> 20) & 0xF;
            op.cond = (first >> 16) & 0xF;
            op.reg = (first >> 12) & 0xF;
            op.compType = (first >> 8) & 0xF;
            switch (op.compType) {
                case 4: op.value = nextVmInt(op.width); break;
                case 5: op.reg2 = (first >> 4) & 0xF; break;
                case 0:
                    op.memType = (first >> 4) & 0xF;
                    op.rel = ((uint64_t)(first & 0xF) << 32) | next();
                    break;
                case 1:
                    op.memType = (first >> 4) & 0xF;
                    op.reg3 = first & 0xF;
                    break;
                case 2:
                    op.reg2 = (first >> 4) & 0xF;
                    op.rel = ((uint64_t)(first & 0xF) << 32) | next();
                    break;
                case 3:
                    op.reg2 = (first >> 4) & 0xF;
                    op.reg3 = first & 0xF;
                    break;
            }
            break;
        default:
            m_stoppedOnUnknown = true;
            return false;
    }
    return valid;
}

void CheatVmEmu::skipBlock(bool isIf) {
    if (m_depth == 0) return;
    const size_t desired = m_depth - 1;
    Op skip;
    while (m_depth > desired && decode(skip)) {
        if (skip.beginCond) {
            m_depth++;
        } else if (skip.type == 0x2) {
            if (!skip.isElse)
                m_depth--;
            else if (isIf && m_depth - 1 == desired)
                break;
        }
    }
}

bool CheatVmEmu::execute(const std::vector<uint32_t>& program, vnx::IGame& mem) {
    m_prog = &program;
    m_ip = 0;
    m_depth = 0;
    m_stoppedOnUnknown = false;
    for (uint64_t& r : regs) r = 0;   // ResetState
    const vnx::ProcessInfo& pi = mem.info();
    Op op;
    while (decode(op)) {
        if (op.beginCond) m_depth++;
        switch (op.type) {
            case 0x0: {
                const uint64_t dst = address(pi, op.memType, op.rel + regs[op.reg]);
                const uint64_t v = op.value;
                if (validWidth(op.width)) mem.write(dst, &v, op.width);
                break;
            }
            case 0x1: {
                uint64_t v = 0;
                if (validWidth(op.width)) mem.read(address(pi, op.memType, op.rel), &v, op.width);
                if (!compare(v, op.cond, maskWidth(op.value, op.width))) skipBlock(true);
                break;
            }
            case 0x2:
                if (op.isElse)
                    skipBlock(false);
                else if (m_depth > 0)
                    m_depth--;
                break;
            case 0x4: regs[op.reg] = op.value; break;
            case 0x5: {
                uint64_t src;
                if (op.loadMode == 1)
                    src = regs[op.reg] + op.rel;
                else if (op.loadMode == 2)
                    src = regs[op.reg2] + op.rel;
                else if (op.loadMode == 3)
                    src = address(pi, op.memType, regs[op.reg2] + op.rel);
                else
                    src = address(pi, op.memType, op.rel);
                // Igual que Atmosphère: solo se sobrescriben los bytes leídos.
                if (validWidth(op.width)) mem.read(src, &regs[op.reg], op.width);
                break;
            }
            case 0x9: {
                const uint64_t a = regs[op.reg2];
                const uint64_t b = op.hasImm ? op.value : regs[op.reg3];
                uint64_t r = 0;
                switch (op.math) {
                    case 0: r = a + b; break;
                    case 1: r = a - b; break;
                    case 2: r = a * b; break;
                    case 3: r = a << b; break;
                    case 4: r = a >> b; break;
                    case 5: r = a & b; break;
                    case 6: r = a | b; break;
                    case 7: r = ~a; break;
                    case 8: r = a ^ b; break;
                    case 9: r = a; break;
                }
                regs[op.reg] = maskWidth(r, op.width);
                break;
            }
            case 0xA: {
                const uint64_t value = regs[op.reg];
                uint64_t dst = regs[op.reg2];
                switch (op.ofsType) {
                    case 1: dst += regs[op.reg3]; break;
                    case 2: dst += op.rel; break;
                    case 3: dst = address(pi, op.memType, regs[op.reg2]); break;
                    case 4: dst = address(pi, op.memType, op.rel); break;
                    case 5: dst = address(pi, op.memType, regs[op.reg2] + op.rel); break;
                    default: break;
                }
                if (validWidth(op.width)) mem.write(dst, &value, op.width);
                if (op.incr) regs[op.reg2] += op.width;
                break;
            }
            case 0xC0: {
                const uint64_t src = maskWidth(regs[op.reg], op.width);
                uint64_t other = 0;
                if (op.compType == 4) {
                    other = maskWidth(op.value, op.width);
                } else if (op.compType == 5) {
                    other = maskWidth(regs[op.reg2], op.width);
                } else {
                    uint64_t addr = 0;
                    switch (op.compType) {
                        case 0: addr = address(pi, op.memType, op.rel); break;
                        case 1: addr = address(pi, op.memType, regs[op.reg3]); break;
                        case 2: addr = regs[op.reg2] + op.rel; break;
                        case 3: addr = regs[op.reg2] + regs[op.reg3]; break;
                    }
                    if (validWidth(op.width)) mem.read(addr, &other, op.width);
                }
                if (!compare(src, op.cond, other)) skipBlock(true);
                break;
            }
            default: break;
        }
    }
    return !m_stoppedOnUnknown;
}
