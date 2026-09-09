#include <iostream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <stack>
#include <bitset>
#include <cstdint>
#include <string>
#include <array>
#include <GLFW/glfw3.h>

using namespace std;

// ============================================================
//  1. VIRTUAL DISK (10 MB)
// ============================================================
class VirtualDisk {
private:
    static const size_t SIZE = 10 * 1024 * 1024;
    vector<uint8_t> data;
    unordered_map<string, size_t> fileTable;
    unordered_map<string, size_t> fileSizes;
    string currentDir = "/";

public:
    VirtualDisk() : data(SIZE, 0) {}

    bool createFile(const string& name) {
        if (fileTable.count(name)) return false;
        size_t offset = 0;
        for (auto& [fname, foffset] : fileTable)
            offset = max(offset, foffset + fileSizes[fname]);
        if (offset + 1024 > SIZE) return false;
        fileTable[name] = offset;
        fileSizes[name] = 0;
        return true;
    }

    bool appendToFile(const string& name, const string& content) {
        if (!fileTable.count(name)) return false;
        size_t offset = fileTable[name] + fileSizes[name];
        if (offset + content.size() > SIZE) return false;
        copy(content.begin(), content.end(), data.begin() + offset);
        fileSizes[name] += content.size();
        return true;
    }

    string readFile(const string& name) {
        if (!fileTable.count(name)) return "";
        size_t offset = fileTable[name];
        size_t size = fileSizes[name];
        return string(data.begin() + offset, data.begin() + offset + size);
    }

    bool writeFile(const string& name, const string& content) {
        if (!fileTable.count(name)) return false;
        fileSizes[name] = 0;
        return appendToFile(name, content);
    }

    void listFiles() {
        cout << "\n--- Files on disk (" << SIZE/1024/1024 << " MB) ---\n";
        for (auto& [name, offset] : fileTable)
            cout << "  " << name << " (" << fileSizes[name] << " bytes)\n";
        cout << "------------------------------------\n";
    }

    bool fileExists(const string& name) { return fileTable.count(name) > 0; }
    size_t getFileSize(const string& name) { return fileSizes[name]; }
    void reset() { data.assign(SIZE, 0); fileTable.clear(); fileSizes.clear(); }
};

// ============================================================
//  2. 16-BIT CPU WITH FLAGS AND CACHE
// ============================================================
class CPU {
private:
    // ---- Registers ----
    uint16_t AX=0, BX=0, CX=0, DX=0;
    uint16_t SP=0xFFFE, BP=0, SI=0, DI=0;
    uint16_t CS=0, DS=0, SS=0, ES=0;
    uint16_t IP = 0;

    // ---- Flags ----
    bool ZF=false, CF=false, SF=false, OF=false, DF=false;
    stack<uint16_t> callStack;

    // ---- Cache ----
    struct CacheLine { bool valid=false; uint32_t tag=0; uint8_t data[64]; };
    static const int CACHE_SIZE = 8;
    CacheLine cache[CACHE_SIZE];

    VirtualDisk* disk;
    bool debugMode = true;

public:
    CPU(VirtualDisk* d) : disk(d) {}

    void setDebug(bool on) { debugMode = on; }

    // ---- Register access for debugging ----
    void printRegs() {
        cout << "AX=0x" << hex << setw(4) << setfill('0') << AX
             << " BX=0x" << setw(4) << BX
             << " CX=0x" << setw(4) << CX
             << " DX=0x" << setw(4) << DX << "\n";
        cout << "SP=0x" << setw(4) << SP << " BP=0x" << setw(4) << BP
             << " SI=0x" << setw(4) << SI << " DI=0x" << setw(4) << DI << "\n";
        cout << "ZF=" << ZF << " CF=" << CF << " SF=" << SF << " OF=" << OF << " DF=" << DF << "\n";
    }

    void reset() {
        AX=BX=CX=DX=SP=BP=SI=DI=CS=DS=SS=ES=IP=0;
        ZF=CF=SF=OF=DF=false;
        while(!callStack.empty()) callStack.pop();
        for(auto& line : cache) line.valid = false;
    }

    // ---- Memory operations (with cache) ----
    uint8_t readByte(uint32_t addr) {
        uint32_t tag = addr / 64, offset = addr % 64;
        for (int i = 0; i < CACHE_SIZE; i++)
            if (cache[i].valid && cache[i].tag == tag)
                return cache[i].data[offset];

        if (debugMode) cout << "[Cache miss] Loading block " << tag << "\n";
        int idx = addr % CACHE_SIZE;
        cache[idx].valid = true;
        cache[idx].tag = tag;
        string content = disk->readFile("program.asm");
        for (size_t i = 0; i < 64 && i + tag * 64 < content.size(); i++)
            cache[idx].data[i] = content[i + tag * 64];
        return cache[idx].data[offset];
    }

    uint16_t readWord(uint32_t addr) {
        return readByte(addr) | (readByte(addr+1) << 8);
    }

    void writeByte(uint32_t addr, uint8_t val) {
        uint32_t tag = addr / 64, offset = addr % 64;
        for (int i = 0; i < CACHE_SIZE; i++)
            if (cache[i].valid && cache[i].tag == tag) {
                cache[i].data[offset] = val;
                return;
            }
        // If not in cache - write directly (simplified)
        string content = disk->readFile("program.asm");
        if (addr < content.size()) content[addr] = val;
        disk->writeFile("program.asm", content);
    }

    void writeWord(uint32_t addr, uint16_t val) {
        writeByte(addr, val & 0xFF);
        writeByte(addr+1, (val >> 8) & 0xFF);
    }

    // ---- Helper functions for arithmetic ----
    void setFlagsForResult(uint16_t result, uint16_t op1, uint16_t op2, bool isAdd) {
        ZF = (result == 0);
        SF = (result & 0x8000) != 0;
        if (isAdd) {
            OF = ((op1 & 0x8000) == (op2 & 0x8000)) &&
                 ((result & 0x8000) != (op1 & 0x8000));
            CF = (result < op1) || (result < op2);
        } else {
            OF = ((op1 & 0x8000) != (op2 & 0x8000)) &&
                 ((result & 0x8000) != (op1 & 0x8000));
            CF = (op1 < op2);
        }
    }

    // ---- Interpreter ----
    void executeProgram(const string& code) {
        if (debugMode) cout << "\n=== EXECUTING PROGRAM ===\n";
        istringstream iss(code);
        string line;
        unordered_map<string, int> labels;
        vector<string> lines;

        // First pass: collect lines and labels
        while (getline(iss, line)) {
            size_t comment = line.find(';');
            if (comment != string::npos) line = line.substr(0, comment);
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            if (line.empty()) continue;

            if (line.back() == ':') {
                labels[line.substr(0, line.size()-1)] = lines.size();
                continue;
            }
            lines.push_back(line);
        }

        // Second pass: execution
        IP = 0;
        while (IP < lines.size()) {
            string instr = lines[IP];
            IP++;
            istringstream cmdStream(instr);
            string opcode;
            cmdStream >> opcode;

            // ---- Instruction handling ----
            if (opcode == "MOV") {
                string dest, src; cmdStream >> dest >> src;
                uint16_t val = getValue(src);
                setRegister(dest, val);
                if (debugMode) cout << "  MOV " << dest << ", " << src << " -> " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "XCHG") {
                string r1, r2; cmdStream >> r1 >> r2;
                uint16_t val1 = getRegister(r1);
                uint16_t val2 = getRegister(r2);
                setRegister(r1, val2);
                setRegister(r2, val1);
                if (debugMode) cout << "  XCHG " << r1 << ", " << r2 << "\n";
            }
            else if (opcode == "PUSH") {
                string src; cmdStream >> src;
                SP -= 2;
                writeWord(SP, getValue(src));
                if (debugMode) cout << "  PUSH " << src << " (SP=0x" << hex << SP << ")\n";
            }
            else if (opcode == "POP") {
                string dest; cmdStream >> dest;
                uint16_t val = readWord(SP);
                SP += 2;
                setRegister(dest, val);
                if (debugMode) cout << "  POP " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "ADD" || opcode == "SUB" || opcode == "CMP") {
                string dest, src; cmdStream >> dest >> src;
                uint16_t val1 = getRegister(dest);
                uint16_t val2 = getValue(src);
                uint16_t result = (opcode == "ADD") ? val1 + val2 :
                                 (opcode == "SUB") ? val1 - val2 : val1 - val2;
                if (opcode != "CMP") setRegister(dest, result);
                setFlagsForResult(result, val1, val2, opcode == "ADD");
                if (debugMode) cout << "  " << opcode << " " << dest << ", " << src << " -> result=0x" << hex << result << "\n";
            }
            else if (opcode == "INC") {
                string dest; cmdStream >> dest;
                uint16_t val = getRegister(dest) + 1;
                setRegister(dest, val);
                ZF = (val == 0); SF = (val & 0x8000) != 0;
                if (debugMode) cout << "  INC " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "DEC") {
                string dest; cmdStream >> dest;
                uint16_t val = getRegister(dest) - 1;
                setRegister(dest, val);
                ZF = (val == 0); SF = (val & 0x8000) != 0;
                if (debugMode) cout << "  DEC " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "NEG") {
                string dest; cmdStream >> dest;
                uint16_t val = -getRegister(dest);
                setRegister(dest, val);
                ZF = (val == 0); SF = (val & 0x8000) != 0; CF = (val != 0);
                if (debugMode) cout << "  NEG " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "MUL" || opcode == "IMUL") {
                string src; cmdStream >> src;
                uint16_t val = getValue(src);
                uint32_t result = AX * val;
                AX = result & 0xFFFF;
                DX = (result >> 16) & 0xFFFF;
                ZF = (AX == 0); SF = (AX & 0x8000) != 0; CF = (DX != 0);
                if (debugMode) cout << "  " << opcode << " " << src << " -> AX=0x" << hex << AX << " DX=0x" << DX << "\n";
            }
            else if (opcode == "DIV" || opcode == "IDIV") {
                string src; cmdStream >> src;
                uint16_t divisor = getValue(src);
                if (divisor == 0) { cout << "  [ERROR] Division by zero!\n"; return; }
                uint32_t dividend = (DX << 16) | AX;
                AX = dividend / divisor;
                DX = dividend % divisor;
                if (debugMode) cout << "  " << opcode << " " << src << " -> AX=0x" << hex << AX << " DX=0x" << DX << "\n";
            }
            else if (opcode == "LOAD") {
                string dest, addr; cmdStream >> dest >> addr;
                uint16_t val = readWord(getValue(addr));
                setRegister(dest, val);
                if (debugMode) cout << "  LOAD " << dest << ", " << addr << " -> " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "STORE") {
                string src, addr; cmdStream >> src >> addr;
                writeWord(getValue(addr), getRegister(src));
                if (debugMode) cout << "  STORE " << src << ", " << addr << "\n";
            }
            else if (opcode == "IN") {
                string dest, port; cmdStream >> dest >> port;
                uint16_t val = 0;
                cout << "  [INPUT] Enter number for port " << port << ": ";
                cin >> val;
                setRegister(dest, val);
                if (debugMode) cout << "  IN " << dest << ", " << port << " -> " << dest << " = 0x" << hex << val << "\n";
            }
            else if (opcode == "OUT") {
                string port, src; cmdStream >> port >> src;
                uint16_t val = getValue(src);
                cout << "  [OUTPUT] Port " << port << " = 0x" << hex << val << " (" << dec << val << ")\n";
                if (debugMode) cout << "  OUT " << port << ", " << src << "\n";
            }
            else if (opcode == "PEEK") {
                string addr; cmdStream >> addr;
                uint16_t val = readByte(getValue(addr));
                cout << "  0x" << hex << setw(4) << setfill('0') << getValue(addr)
                     << " = 0x" << setw(2) << (int)val << "\n";
            }
            else if (opcode == "POKE") {
                string addr, val; cmdStream >> addr >> val;
                writeByte(getValue(addr), getValue(val) & 0xFF);
                if (debugMode) cout << "  POKE " << addr << ", " << val << "\n";
            }
            else if (opcode == "DUMP") {
                string addr, len; cmdStream >> addr >> len;
                uint32_t start = getValue(addr);
                uint32_t length = getValue(len);
                for (uint32_t i = 0; i < length; i += 16) {
                    cout << "0x" << hex << setw(6) << setfill('0') << (start+i) << ": ";
                    for (uint32_t j = 0; j < 16 && i+j < length; j++)
                        cout << setw(2) << (int)readByte(start+i+j) << " ";
                    cout << "\n";
                }
            }
            else if (opcode == "PRINT") {
                string arg; cmdStream >> arg;
                if (arg[0] == '"' && arg.back() == '"') {
                    cout << "  " << arg.substr(1, arg.size()-2) << "\n";
                } else {
                    uint16_t val = getValue(arg);
                    cout << "  0x" << hex << val << " (" << dec << val << ")\n";
                }
            }
            else if (opcode == "CLC") { CF = false; if (debugMode) cout << "  CLC (CF=0)\n"; }
            else if (opcode == "STC") { CF = true;  if (debugMode) cout << "  STC (CF=1)\n"; }
            else if (opcode == "CMC") { CF = !CF;   if (debugMode) cout << "  CMC (CF=" << CF << ")\n"; }
            else if (opcode == "CLD") { DF = false; if (debugMode) cout << "  CLD (DF=0)\n"; }
            else if (opcode == "STD") { DF = true;  if (debugMode) cout << "  STD (DF=1)\n"; }
            else if (opcode == "NOP") { if (debugMode) cout << "  NOP\n"; }
            else if (opcode == "HLT") { if (debugMode) cout << "  [HLT] Program terminated\n"; return; }
            else if (opcode == "JMP") {
                string label; cmdStream >> label;
                if (labels.count(label)) { IP = labels[label]; if (debugMode) cout << "  JMP " << label << "\n"; }
                else { cout << "  [ERROR] Label " << label << " not found\n"; return; }
            }
            else if (opcode == "JE" || opcode == "JZ") {
                string label; cmdStream >> label;
                if (ZF && labels.count(label)) { IP = labels[label]; if (debugMode) cout << "  " << opcode << " " << label << " (ZF=1)\n"; }
            }
            else if (opcode == "JNE" || opcode == "JNZ") {
                string label; cmdStream >> label;
                if (!ZF && labels.count(label)) { IP = labels[label]; if (debugMode) cout << "  " << opcode << " " << label << " (ZF=0)\n"; }
            }
            else if (opcode == "JG") {
                string label; cmdStream >> label;
                if (!ZF && !SF && !OF && labels.count(label)) { IP = labels[label]; if (debugMode) cout << "  JG " << label << "\n"; }
            }
            else if (opcode == "JL") {
                string label; cmdStream >> label;
                if (SF != OF && labels.count(label)) { IP = labels[label]; if (debugMode) cout << "  JL " << label << "\n"; }
            }
            else if (opcode == "CALL") {
                string label; cmdStream >> label;
                if (labels.count(label)) {
                    callStack.push(IP);
                    IP = labels[label];
                    if (debugMode) cout << "  CALL " << label << "\n";
                } else { cout << "  [ERROR] Label " << label << " not found\n"; return; }
            }
            else if (opcode == "RET") {
                if (!callStack.empty()) { IP = callStack.top(); callStack.pop(); if (debugMode) cout << "  RET\n"; }
                else { cout << "  [ERROR] RET without CALL\n"; return; }
            }
            else if (opcode == "LOOP") {
                string label; cmdStream >> label;
                CX--;
                if (CX != 0 && labels.count(label)) { IP = labels[label]; if (debugMode) cout << "  LOOP " << label << " (CX=" << CX << ")\n"; }
            }
            else {
                cout << "  [ERROR] Unknown instruction: " << opcode << "\n";
                return;
            }
        }
    }

private:
    uint16_t getValue(const string& arg) {
        if (arg.empty()) return 0;
        if (arg[0] == 'R' && arg.size()>1 && isdigit(arg[1])) {
            int num = arg[1]-'0';
            if (num >= 0 && num <= 7) {
                uint16_t regs[] = {AX, BX, CX, DX, SP, BP, SI, DI};
                return regs[num];
            }
        }
        if (arg == "AX" || arg == "BX" || arg == "CX" || arg == "DX" ||
            arg == "SP" || arg == "BP" || arg == "SI" || arg == "DI") {
            return getRegister(arg);
        }
        if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
            return stoi(arg.substr(2), nullptr, 16);
        }
        return stoi(arg);
    }

    uint16_t getRegister(const string& reg) {
        if (reg == "AX") return AX;
        if (reg == "BX") return BX;
        if (reg == "CX") return CX;
        if (reg == "DX") return DX;
        if (reg == "SP") return SP;
        if (reg == "BP") return BP;
        if (reg == "SI") return SI;
        if (reg == "DI") return DI;
        return 0;
    }

    void setRegister(const string& reg, uint16_t val) {
        if (reg == "AX") { AX = val; return; } if (reg == "BX") { BX = val; return; }
        if (reg == "CX") { CX = val; return; } if (reg == "DX") { DX = val; return; }
        if (reg == "SP") { SP = val; return; } if (reg == "BP") { BP = val; return; }
        if (reg == "SI") { SI = val; return; } if (reg == "DI") { DI = val; return; }
    }
};

// ============================================================
//  3. FULL-SCREEN GLFW TERMINAL INTERFACE
// ============================================================
class TerminalInterface {
private:
    VirtualDisk disk;
    CPU cpu;
    GLFWwindow* window = nullptr;
    string editor =
        "; HELLO FROM THE 16-BIT CPU\n"
        "MOV AX 0x002A\n"
        "MOV BX 0x0008\n"
        "ADD AX BX\n"
        "OUT 1 AX\n"
        "HLT\n";
    string log = "SYSTEM READY.  EDIT THE PROGRAM AND PRESS F5 TO RUN.\n";
    string status = "ONLINE";
    bool showHelp = false;
    bool cursorVisible = true;
    double lastBlink = 0.0;

    void appendLog(const string& message) {
        log += message;
        if (!log.empty() && log.back() != '\n') log += '\n';
        constexpr size_t maxLogLength = 2600;
        if (log.size() > maxLogLength) log.erase(0, log.size() - maxLogLength);
    }

    void runProgram() {
        status = "EXECUTING";
        cpu.setDebug(true);
        ostringstream captured;
        streambuf* oldBuffer = cout.rdbuf(captured.rdbuf());
        cpu.executeProgram(editor);
        cout.rdbuf(oldBuffer);
        appendLog("[EXEC] PROGRAM FINISHED");
        appendLog(captured.str());
        status = "ONLINE";
    }

    void resetMachine() {
        disk.reset();
        cpu.reset();
        status = "RESET COMPLETE";
        appendLog("[SYSTEM] CPU, CACHE AND VIRTUAL DISK RESET.");
    }

    static void errorCallback(int, const char* description) {
        cerr << "GLFW error: " << description << '\n';
    }

    static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        auto* app = static_cast<TerminalInterface*>(glfwGetWindowUserPointer(window));
        if (!app) return;
        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_F10) glfwSetWindowShouldClose(window, GLFW_TRUE);
        else if (key == GLFW_KEY_F5) app->runProgram();
        else if (key == GLFW_KEY_F2) app->resetMachine();
        else if (key == GLFW_KEY_F1) app->showHelp = !app->showHelp;
        else if (key == GLFW_KEY_BACKSPACE && !app->editor.empty()) app->editor.pop_back();
        else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) app->editor += '\n';
    }

    static void charCallback(GLFWwindow* window, unsigned int codepoint) {
        auto* app = static_cast<TerminalInterface*>(glfwGetWindowUserPointer(window));
        if (!app || codepoint < 32 || codepoint > 126) return;
        app->editor += static_cast<char>(codepoint);
    }

    static void mouseCallback(GLFWwindow* window, int button, int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
        auto* app = static_cast<TerminalInterface*>(glfwGetWindowUserPointer(window));
        if (!app) return;
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        const float top = static_cast<float>(height) - 78.0f;
        if (y >= top && y <= top + 40.0f) {
            if (x >= width - 310 && x < width - 155) app->resetMachine();
            else if (x >= width - 145 && x <= width - 20) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    static const array<uint8_t, 7>& glyph(char c) {
        static const array<uint8_t, 7> empty{};
        static const unordered_map<char, array<uint8_t, 7>> font = {
            {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
            {'C',{0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}}, {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
            {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
            {'G',{0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
            {'I',{0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}}, {'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
            {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
            {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
            {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
            {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
            {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
            {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
            {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
            {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
            {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
            {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
            {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
            {'6',{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
            {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
            {'-',{0,0,0,0x1F,0,0,0}}, {'_',{0,0,0,0,0,0,0x1F}}, {'=',{0,0x1F,0,0x1F,0,0,0}},
            {'.',{0,0,0,0,0,0x06,0x06}}, {':',{0,0x06,0x06,0,0x06,0x06,0}}, {'/',{0x01,0x02,0x04,0x08,0x10,0,0}},
            {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
            {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}}, {'>',{0x10,0x08,0x04,0x02,0x04,0x08,0x10}},
            {'<',{0x01,0x02,0x04,0x08,0x04,0x02,0x01}}, {'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},
            {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
            {',',{0,0,0,0,0x06,0x06,0x04}}, {';',{0,0x06,0x06,0,0x06,0x06,0x04}}, {'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},
            {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}}, {'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}}
        };
        auto it = font.find(static_cast<char>(toupper(static_cast<unsigned char>(c))));
        return it == font.end() ? empty : it->second;
    }

    static void rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
        glColor4f(r, g, b, a); glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h); glEnd();
    }

    static void text(float x, float y, const string& value, float scale, float r, float g, float b) {
        const float start = x, advance = 6.0f * scale, line = 9.0f * scale;
        glColor3f(r, g, b); glBegin(GL_QUADS);
        for (char c : value) {
            if (c == '\n') { x = start; y += line; continue; }
            for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
                if (glyph(c)[row] & (1 << (4 - col))) {
                    const float px = x + col * scale, py = y + row * scale;
                    glVertex2f(px, py); glVertex2f(px + scale, py); glVertex2f(px + scale, py + scale); glVertex2f(px, py + scale);
                }
            x += advance;
        }
        glEnd();
    }

    static vector<string> linesFor(const string& value, size_t maxColumns, size_t maxLines) {
        vector<string> lines;
        istringstream stream(value); string line;
        while (getline(stream, line) && lines.size() < maxLines) {
            if (line.empty()) { lines.push_back(" "); continue; }
            while (line.size() > maxColumns && lines.size() < maxLines) { lines.push_back(line.substr(0, maxColumns)); line.erase(0, maxColumns); }
            if (lines.size() < maxLines) lines.push_back(line);
        }
        return lines;
    }

    void drawPanel(float x, float y, float w, float h, const string& title) const {
        rect(x, y, w, h, 0.015f, 0.08f, 0.045f);
        rect(x, y, w, 1.5f, 0.12f, 0.9f, 0.45f);
        text(x + 10, y + 8, "[ " + title + " ]", 2.0f, 0.45f, 1.0f, 0.62f);
    }

    void draw(int width, int height) {
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glClearColor(0.0f, 0.018f, 0.008f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
        for (int y = 0; y < height; y += 4) rect(0, static_cast<float>(y), static_cast<float>(width), 1, 0.0f, 0.08f, 0.025f, 0.28f);

        rect(18, 18, static_cast<float>(width - 36), 50, 0.02f, 0.16f, 0.07f);
        text(36, 34, "CPU/16 :: GREEN PHOSPHOR WORKSTATION", 2.3f, 0.55f, 1.0f, 0.64f);
        text(static_cast<float>(width - 278), 37, "STATUS: " + status, 1.6f, 0.45f, 1.0f, 0.56f);

        const float top = 86, bottom = static_cast<float>(height - 24), usable = bottom - top;
        const float leftW = width * 0.57f, gap = 14, rightX = 18 + leftW + gap, rightW = width - rightX - 18;
        drawPanel(18, top, leftW, usable, "PROGRAM.ASM  //  EDITOR");
        drawPanel(rightX, top, rightW, usable * 0.62f, "EXECUTION LOG");
        drawPanel(rightX, top + usable * 0.64f, rightW, usable * 0.36f, "CONTROL DECK");

        const float codeScale = max(1.5f, min(2.3f, width / 750.0f));
        auto sourceLines = linesFor(editor, static_cast<size_t>((leftW - 45) / (6 * codeScale)), static_cast<size_t>((usable - 58) / (9 * codeScale)));
        float y = top + 48;
        for (size_t i = 0; i < sourceLines.size(); ++i) {
            ostringstream number; number << setw(2) << setfill('0') << i + 1;
            text(32, y, number.str(), codeScale, 0.13f, 0.46f, 0.24f);
            text(68, y, sourceLines[i], codeScale, 0.58f, 1.0f, 0.66f); y += 9 * codeScale;
        }
        if (cursorVisible && sourceLines.size() < 22) text(68 + (sourceLines.empty() ? 0 : sourceLines.back().size() * 6 * codeScale), y - 9 * codeScale, "_", codeScale, 0.7f, 1.0f, 0.72f);

        const float logScale = max(1.3f, min(1.8f, rightW / 390.0f));
        auto logLines = linesFor(log, static_cast<size_t>((rightW - 30) / (6 * logScale)), static_cast<size_t>((usable * .62f - 58) / (9 * logScale)));
        y = top + 48;
        for (const auto& line : logLines) { text(rightX + 14, y, line, logScale, 0.35f, 0.9f, 0.48f); y += 9 * logScale; }

        const float controlsY = top + usable * .64f + 48;
        text(rightX + 14, controlsY, "F5  RUN PROGRAM", 1.8f, 0.55f, 1.0f, 0.62f);
        text(rightX + 14, controlsY + 26, "F2  REBOOT MACHINE", 1.8f, 0.40f, 0.95f, 0.55f);
        text(rightX + 14, controlsY + 52, "F1  HELP OVERLAY", 1.8f, 0.40f, 0.95f, 0.55f);
        text(rightX + 14, controlsY + 78, "F10 / ESC  POWER OFF", 1.8f, 0.40f, 0.95f, 0.55f);
        if (showHelp) {
            rect(70, 95, static_cast<float>(width - 140), static_cast<float>(height - 190), 0.0f, 0.12f, 0.045f, 0.97f);
            text(100, 130, "CPU/16 FIELD MANUAL", 3.0f, 0.60f, 1.0f, 0.67f);
            text(100, 185, "TYPE DIRECTLY IN THE LEFT EDITOR.\nF5 RUNS THE ASSEMBLY PROGRAM.\nF2 RESETS CPU, CACHE AND DISK.\nCLICK REBOOT OR POWER OFF IN THE FOOTER.\nPRESS F1 TO CLOSE THIS OVERLAY.", 2.2f, 0.48f, 1.0f, 0.60f);
        }
        rect(static_cast<float>(width - 310), static_cast<float>(height - 78), 155, 40, 0.04f, 0.28f, 0.10f);
        rect(static_cast<float>(width - 145), static_cast<float>(height - 78), 125, 40, 0.22f, 0.06f, 0.04f);
        text(static_cast<float>(width - 298), static_cast<float>(height - 66), "REBOOT [F2]", 1.6f, 0.6f, 1.0f, 0.65f);
        text(static_cast<float>(width - 133), static_cast<float>(height - 66), "POWER OFF", 1.6f, 1.0f, 0.56f, 0.48f);
    }

public:
    TerminalInterface() : cpu(&disk) {}

    int run() {
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit()) return 1;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) { glfwTerminate(); return 1; }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        window = glfwCreateWindow(mode->width, mode->height, "CPU/16 Green Phosphor", monitor, nullptr);
        if (!window) { glfwTerminate(); return 1; }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        glfwSetCharCallback(window, charCallback);
        glfwSetMouseButtonCallback(window, mouseCallback);
        while (!glfwWindowShouldClose(window)) {
            const double now = glfwGetTime();
            if (now - lastBlink > 0.55) { cursorVisible = !cursorVisible; lastBlink = now; }
            int width, height; glfwGetFramebufferSize(window, &width, &height);
            draw(width, height); glfwSwapBuffers(window); glfwPollEvents();
        }
        glfwDestroyWindow(window); glfwTerminate();
        return 0;
    }
};

int main() {
    TerminalInterface terminal;
    return terminal.run();
}
