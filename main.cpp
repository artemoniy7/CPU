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
        else if (key == GLFW_KEY_F1) app->showHelp = !app->showHelp;
        else if (app->showHelp) return;
        else if (key == GLFW_KEY_F5) app->runProgram();
        else if (key == GLFW_KEY_F2) app->resetMachine();
        else if (key == GLFW_KEY_BACKSPACE && !app->editor.empty()) app->editor.pop_back();
        else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) app->editor += '\n';
    }

    static void charCallback(GLFWwindow* window, unsigned int codepoint) {
        auto* app = static_cast<TerminalInterface*>(glfwGetWindowUserPointer(window));
        if (!app || app->showHelp || codepoint < 32 || codepoint > 126) return;
        app->editor += static_cast<char>(codepoint);
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

    void draw(int width, int height) {
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();

        // Desk-like backdrop and a warm, thick CRT enclosure inspired by 1980s terminals.
        glClearColor(0.055f, 0.035f, 0.022f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
        for (int y = 0; y < height; y += 6)
            rect(0, static_cast<float>(y), static_cast<float>(width), 1, 0.12f, 0.075f, 0.035f, 0.22f);

        const float scale = min(width / 1280.0f, height / 900.0f);
        const float caseW = 930 * scale, caseH = 745 * scale;
        const float caseX = (width - caseW) / 2, caseY = (height - caseH) / 2 - 16 * scale;
        const float bezel = 58 * scale;
        const float screenX = caseX + bezel, screenY = caseY + 82 * scale;
        const float screenW = caseW - 2 * bezel, screenH = 500 * scale;

        // Case shadow, cream plastic, bevel and the nearly-black curved-screen approximation.
        rect(caseX + 16 * scale, caseY + 20 * scale, caseW, caseH, 0.025f, 0.014f, 0.008f, 0.72f);
        rect(caseX, caseY, caseW, caseH, 0.62f, 0.47f, 0.28f);
        rect(caseX + 10 * scale, caseY + 10 * scale, caseW - 20 * scale, caseH - 20 * scale, 0.78f, 0.63f, 0.39f);
        rect(screenX - 16 * scale, screenY - 16 * scale, screenW + 32 * scale, screenH + 32 * scale, 0.13f, 0.10f, 0.055f);
        rect(screenX - 7 * scale, screenY - 7 * scale, screenW + 14 * scale, screenH + 14 * scale, 0.025f, 0.035f, 0.026f);
        rect(screenX, screenY, screenW, screenH, 0.0f, 0.075f, 0.039f);
        for (int y = static_cast<int>(screenY); y < screenY + screenH; y += max(2, static_cast<int>(4 * scale)))
            rect(screenX, static_cast<float>(y), screenW, 1, 0.0f, 0.0f, 0.0f, 0.36f);
        rect(screenX, screenY, screenW, 2 * scale, 0.15f, 0.95f, 0.52f, 0.56f);

        const float terminalScale = max(1.35f, 2.0f * scale);
        const size_t columns = static_cast<size_t>((screenW - 36 * scale) / (6 * terminalScale));
        const size_t rows = static_cast<size_t>((screenH - 60 * scale) / (9 * terminalScale));
        text(screenX + 18 * scale, screenY + 16 * scale,
             "CPU-16 MICROCOMPUTER  //  MONITOR 1  //  " + status,
             terminalScale, 0.48f, 1.0f, 0.62f);
        text(screenX + 18 * scale, screenY + 38 * scale,
             "PROGRAM.ASM  READY   |   F5 RUN   F2 RESET   F1 HELP   F10 OFF",
             terminalScale * .83f, 0.26f, 0.72f, 0.39f);

        string display = showHelp
            ? "*** CPU-16 TERMINAL HELP ***\n\nTYPE ASSEMBLY DIRECTLY AT THE END OF THE PROGRAM.\nBACKSPACE REMOVES A CHARACTER; ENTER STARTS A NEW LINE.\n\nF5   EXECUTE PROGRAM\nF2   RESET CPU, CACHE AND DISK\nF1   RETURN TO TERMINAL\nF10  POWER OFF TERMINAL\n\nNO MOUSE REQUIRED."
            : editor + "\n> " + log;
        auto terminalLines = linesFor(display, columns, rows);
        float y = screenY + 62 * scale;
        for (const auto& line : terminalLines) {
            text(screenX + 18 * scale, y, line, terminalScale, 0.38f, 0.95f, 0.51f);
            y += 9 * terminalScale;
        }
        if (!showHelp && cursorVisible && terminalLines.size() < rows)
            text(screenX + 18 * scale, y, "_", terminalScale, 0.70f, 1.0f, 0.70f);

        // Physical terminal details: maker plate, vents, power lamp, and keyboard-only legend.
        rect(caseX + 34 * scale, caseY + caseH - 120 * scale, 250 * scale, 42 * scale, 0.48f, 0.35f, 0.20f);
        text(caseX + 48 * scale, caseY + caseH - 107 * scale, "A R C A D E  1 6", 1.3f * scale, 0.12f, 0.09f, 0.05f);
        text(caseX + 48 * scale, caseY + caseH - 91 * scale, "PERSONAL COMPUTER", 0.85f * scale, 0.12f, 0.09f, 0.05f);
        for (int i = 0; i < 5; ++i)
            rect(caseX + 38 * scale + i * 10 * scale, caseY + caseH - 55 * scale, 6 * scale, 3 * scale, 0.18f, 0.13f, 0.07f);
        rect(caseX + caseW - 104 * scale, caseY + caseH - 108 * scale, 52 * scale, 26 * scale, 0.18f, 0.13f, 0.07f);
        rect(caseX + caseW - 97 * scale, caseY + caseH - 101 * scale, 12 * scale, 12 * scale, 0.22f, 0.95f, 0.31f);
        text(caseX + caseW - 166 * scale, caseY + caseH - 68 * scale, "POWER", 0.9f * scale, 0.18f, 0.13f, 0.07f);

        rect(caseX + 122 * scale, caseY + caseH + 12 * scale, caseW - 244 * scale, 54 * scale, 0.43f, 0.32f, 0.19f);
        text(caseX + 150 * scale, caseY + caseH + 30 * scale,
             "[F5] RUN     [F2] REBOOT     [F1] HELP     [F10] POWER OFF",
             1.35f * scale, 0.76f, 0.64f, 0.40f);
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
