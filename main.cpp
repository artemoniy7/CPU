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
        for (int i = 0; i < 64 && i < content.size(); i++)
            cache[idx].data[i] = content[i + tag*64];
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
        int lineNum = 0;
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
        if (reg == "AX") return AX; if (reg == "BX") return BX;
        if (reg == "CX") return CX; if (reg == "DX") return DX;
        if (reg == "SP") return SP; if (reg == "BP") return BP;
        if (reg == "SI") return SI; if (reg == "DI") return DI;
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
//  3. CONSOLE INTERFACE
// ============================================================
class ConsoleInterface {
private:
    VirtualDisk disk;
    CPU cpu;
    bool running = true;
    string currentFile = "";
    bool debugMode = true;

    void showHelp() {
        cout << "\n========================================\n";
        cout << "  COMMAND LIST\n";
        cout << "========================================\n";
        cout << "  PROGRAM <file> = <code> | <code> | ...\n";
        cout << "    Create a program file with multiple lines\n";
        cout << "    Example: PROGRAM test.asm = MOV AX 10 | ADD AX 20 | HLT\n";
        cout << "\n  APPEND <file> <code> | <code> | ...\n";
        cout << "    Append lines to an existing file\n";
        cout << "\n  RUN <file>\n";
        cout << "    Execute the program from file\n";
        cout << "\n  SHOW <file>\n";
        cout << "    Display the contents of a file\n";
        cout << "\n  REGS\n";
        cout << "    Show all registers and flags\n";
        cout << "\n  RESET\n";
        cout << "    Reset CPU and disk to initial state\n";
        cout << "\n  DEBUG ON/OFF\n";
        cout << "    Enable or disable debug output\n";
        cout << "\n  DUMP <addr> <length>\n";
        cout << "    Dump memory from address with given length\n";
        cout << "    Example: DUMP 0x100 16\n";
        cout << "\n  PEEK <addr>\n";
        cout << "    Read a byte from memory address\n";
        cout << "    Example: PEEK 0x200\n";
        cout << "\n  LS\n";
        cout << "    List all files on the virtual disk\n";
        cout << "\n  HELP\n";
        cout << "    Show this help message\n";
        cout << "\n  EXIT\n";
        cout << "    Exit the emulator\n";
        cout << "========================================\n";
    }

public:
    ConsoleInterface() : cpu(&disk) {}

    void run() {
        cout << "========================================\n";
        cout << "  16-BIT VIRTUAL COMPUTER v2.0\n";
        cout << "  Disk: 10 MB, Cache: 512 bytes\n";
        cout << "  Registers: AX BX CX DX SP BP SI DI\n";
        cout << "  Flags: ZF CF SF OF DF\n";
        cout << "========================================\n";
        cout << "Type HELP for command list\n";
        cout << "========================================\n";

        while (running) {
            cout << "\n> ";
            string command;
            getline(cin, command);
            if (command.empty()) continue;

            istringstream iss(command);
            string cmd;
            iss >> cmd;

            // Convert to uppercase for case-insensitive commands
            string cmdUpper = cmd;
            transform(cmdUpper.begin(), cmdUpper.end(), cmdUpper.begin(), ::toupper);

            if (cmdUpper == "PROGRAM" || cmdUpper == "PROG") {
                string filename, eq, code;
                iss >> filename >> eq;
                getline(iss, code);
                if (eq != "=") { cout << "Expected '='\n"; continue; }
                code.erase(0, code.find_first_not_of(" \t"));
                if (disk.createFile(filename)) {
                    string line;
                    istringstream codeStream(code);
                    while (getline(codeStream, line, '|')) {
                        line.erase(0, line.find_first_not_of(" \t"));
                        disk.appendToFile(filename, line + "\n");
                    }
                    cout << "Program " << filename << " created\n";
                } else cout << "Create error\n";
            }
            else if (cmdUpper == "APPEND") {
                string filename, code;
                iss >> filename;
                getline(iss, code);
                code.erase(0, code.find_first_not_of(" \t"));
                if (!disk.fileExists(filename)) { cout << "File not found\n"; continue; }
                string line;
                istringstream codeStream(code);
                while (getline(codeStream, line, '|')) {
                    line.erase(0, line.find_first_not_of(" \t"));
                    disk.appendToFile(filename, line + "\n");
                }
                cout << "Lines appended to " << filename << "\n";
            }
            else if (cmdUpper == "RUN" || cmdUpper == "EXEC") {
                string filename; iss >> filename;
                if (disk.fileExists(filename)) {
                    cpu.setDebug(debugMode);
                    cpu.executeProgram(disk.readFile(filename));
                } else cout << "File not found\n";
            }
            else if (cmdUpper == "SHOW" || cmdUpper == "TYPE") {
                string filename; iss >> filename;
                if (disk.fileExists(filename)) {
                    cout << "\n--- " << filename << " ---\n";
                    cout << disk.readFile(filename);
                    cout << "-----------------\n";
                } else cout << "File not found\n";
            }
            else if (cmdUpper == "REGS") {
                cpu.printRegs();
            }
            else if (cmdUpper == "RESET") {
                disk.reset();
                cpu.reset();
                cout << "Reset complete\n";
            }
            else if (cmdUpper == "DEBUG" || cmdUpper == "DBG") {
                string mode; iss >> mode;
                string modeUpper = mode;
                transform(modeUpper.begin(), modeUpper.end(), modeUpper.begin(), ::toupper);
                if (modeUpper == "ON") { debugMode = true; cpu.setDebug(true); cout << "Debug ON\n"; }
                else if (modeUpper == "OFF") { debugMode = false; cpu.setDebug(false); cout << "Debug OFF\n"; }
                else cout << "Use DEBUG ON/OFF\n";
            }
            else if (cmdUpper == "DUMP") {
                string addr, len; iss >> addr >> len;
                uint32_t start = stoi(addr);
                uint32_t length = stoi(len);
                for (uint32_t i = 0; i < length; i += 16) {
                    cout << "0x" << hex << setw(6) << setfill('0') << (start+i) << ": ";
                    for (uint32_t j = 0; j < 16 && i+j < length; j++)
                        cout << setw(2) << (int)disk.readFile("program.asm")[start+i+j] << " ";
                    cout << "\n";
                }
            }
            else if (cmdUpper == "PEEK") {
                string addr; iss >> addr;
                uint32_t a = stoi(addr);
                cout << "0x" << hex << a << " = 0x" << setw(2) << (int)disk.readFile("program.asm")[a] << "\n";
            }
            else if (cmdUpper == "LS" || cmdUpper == "DIR") {
                disk.listFiles();
            }
            else if (cmdUpper == "HELP" || cmdUpper == "H" || cmdUpper == "?") {
                showHelp();
            }
            else if (cmdUpper == "EXIT" || cmdUpper == "QUIT") {
                running = false;
                cout << "Exiting...\n";
            }
            else {
                cout << "Unknown command. Type HELP for list.\n";
            }
        }
    }
};

int main() {
    ConsoleInterface console;
    console.run();
    return 0;
}