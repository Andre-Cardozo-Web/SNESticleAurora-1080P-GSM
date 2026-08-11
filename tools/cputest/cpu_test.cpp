#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "types.h"
#include "sncpu.h"
#include "sncpu_c.h"
}

static Uint32 g_TrapReadAddress;
static Uint32 g_TrapWriteAddress;
static Uint8 g_TrapWriteData;

extern "C" Uint8 SNCPU_TRAPFUNC TestTrapRead(SNCpuT *, Uint32 address)
{
	g_TrapReadAddress = address;
	return 0xA5;
}

extern "C" void SNCPU_TRAPFUNC TestTrapWrite(SNCpuT *, Uint32 address,
	Uint8 data)
{
	g_TrapWriteAddress = address;
	g_TrapWriteData = data;
}

static bool Check24BitBusWrap(SNCpuT *cpu, Uint8 *memory)
{
	bool ok = true;

	memory[0x0123] = 0x5A;
	ok &= SNCPURead8(cpu, 0x1000123) == 0x5A;
	SNCPUWrite8(cpu, 0x1000123, 0xC3);
	ok &= memory[0x0123] == 0xC3;

	/* The overflow page must also forward I/O/trap accesses with the masked
	   24-bit address, rather than exposing $100:xxxx to the device. */
	SNCPUSetTrap(cpu, 0x2000, SNCPU_BANK_SIZE, TestTrapRead, TestTrapWrite);
	SNCPUMirror24BitBus(cpu);
	g_TrapReadAddress = 0xFFFFFFFF;
	g_TrapWriteAddress = 0xFFFFFFFF;
	g_TrapWriteData = 0;
	ok &= SNCPURead8(cpu, 0x1002345) == 0xA5;
	ok &= g_TrapReadAddress == 0x2345;
	SNCPUWrite8(cpu, 0x1002345, 0x7E);
	ok &= g_TrapWriteAddress == 0x2345 && g_TrapWriteData == 0x7E;

	SNCPUSetBank(cpu, 0x2000, SNCPU_BANK_SIZE, memory + 0x2000, TRUE);
	SNCPUSetMemSpeed(cpu, 0x2000, SNCPU_BANK_SIZE, SNCPU_CYCLE_FAST);
	SNCPUMirror24BitBus(cpu);
	memory[0x0123] = 0;

	std::printf("24-bit bus wrap: %s\n", ok ? "PASS" : "FAIL");
	return ok;
}

static unsigned ParseNumber(const std::string &text)
{
	return (unsigned)std::strtoul(text.c_str(), NULL, 10);
}

static void Split(const std::string &text, char separator,
	std::vector<std::string> &parts)
{
	std::string part;
	std::istringstream input(text);

	parts.clear();
	while (std::getline(input, part, separator))
		parts.push_back(part);
}

static void LoadRam(Uint8 *memory, const std::string &spec,
	std::vector<Uint32> &touched)
{
	std::vector<std::string> pairs;
	Split(spec, ';', pairs);
	for (size_t i = 0; i < pairs.size(); i++)
	{
		size_t equals = pairs[i].find('=');
		if (equals == std::string::npos)
			continue;
		Uint32 address = ParseNumber(pairs[i].substr(0, equals)) & 0xFFFFFF;
		memory[address] = (Uint8)ParseNumber(pairs[i].substr(equals + 1));
		touched.push_back(address);
	}
}

static bool CheckRam(const Uint8 *memory, const std::string &spec)
{
	std::vector<std::string> pairs;
	Split(spec, ';', pairs);
	for (size_t i = 0; i < pairs.size(); i++)
	{
		size_t equals = pairs[i].find('=');
		if (equals == std::string::npos)
			continue;
		Uint32 address = ParseNumber(pairs[i].substr(0, equals)) & 0xFFFFFF;
		Uint8 expected = (Uint8)ParseNumber(pairs[i].substr(equals + 1));
		if (memory[address] != expected)
			return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	const unsigned maxFailures = argc > 1 ? ParseNumber(argv[1]) : 20;
	Uint8 *memory = (Uint8 *)std::calloc(SNCPU_MEM_SIZE, 1);
	SNCpuT cpu;
	std::string line;
	unsigned tests = 0;
	unsigned failures = 0;

	if (!memory)
		return 2;
	SNCPUNew(&cpu);
	SNCPUSetBank(&cpu, 0, SNCPU_MEM_SIZE, memory, TRUE);
	SNCPUSetMemSpeed(&cpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_FAST);
	SNCPUMirror24BitBus(&cpu);
	SNCPUSetExecuteFunc(SNCPUExecute_C);
	if (!Check24BitBusWrap(&cpu, memory))
	{
		std::free(memory);
		return 1;
	}

	while (std::getline(std::cin, line))
	{
		std::vector<std::string> field;
		std::vector<Uint32> touched;
		Split(line, '\t', field);
		if (field.size() != 23)
		{
			std::fprintf(stderr, "bad input: got %u fields\n", (unsigned)field.size());
			return 2;
		}

		LoadRam(memory, field[21], touched);
		cpu.Regs.rPC = (ParseNumber(field[9]) << 16) | ParseNumber(field[1]);
		cpu.Regs.rS.w = (Uint16)ParseNumber(field[2]);
		cpu.Regs.rP = (Uint8)ParseNumber(field[3]);
		cpu.Regs.rA.w = (Uint16)ParseNumber(field[4]);
		cpu.Regs.rX.w = (Uint16)ParseNumber(field[5]);
		cpu.Regs.rY.w = (Uint16)ParseNumber(field[6]);
		cpu.Regs.rDB = ParseNumber(field[7]) << 16;
		cpu.Regs.rDP = (Uint16)ParseNumber(field[8]);
		cpu.Regs.rE = (Uint8)ParseNumber(field[10]);
		cpu.uSignal = 0;
		cpu.Cycles = 1000;
		SNCPUExecuteOne(&cpu);

		bool ok =
			(cpu.Regs.rPC & 0xFFFF) == ParseNumber(field[11]) &&
			cpu.Regs.rS.w == ParseNumber(field[12]) &&
			cpu.Regs.rP == ParseNumber(field[13]) &&
			cpu.Regs.rA.w == ParseNumber(field[14]) &&
			cpu.Regs.rX.w == ParseNumber(field[15]) &&
			cpu.Regs.rY.w == ParseNumber(field[16]) &&
			((cpu.Regs.rDB >> 16) & 0xFF) == ParseNumber(field[17]) &&
			cpu.Regs.rDP == ParseNumber(field[18]) &&
			((cpu.Regs.rPC >> 16) & 0xFF) == ParseNumber(field[19]) &&
			cpu.Regs.rE == ParseNumber(field[20]) &&
			CheckRam(memory, field[22]);

		tests++;
		if (!ok)
		{
			failures++;
			if (failures <= maxFailures)
			{
				std::printf("FAIL %s got pc=%02X:%04X s=%04X p=%02X a=%04X x=%04X y=%04X db=%02X d=%04X e=%u\n",
					field[0].c_str(), (unsigned)(cpu.Regs.rPC >> 16) & 0xFF,
					(unsigned)cpu.Regs.rPC & 0xFFFF, (unsigned)cpu.Regs.rS.w,
					(unsigned)cpu.Regs.rP, (unsigned)cpu.Regs.rA.w,
					(unsigned)cpu.Regs.rX.w, (unsigned)cpu.Regs.rY.w,
					(unsigned)(cpu.Regs.rDB >> 16) & 0xFF,
					(unsigned)cpu.Regs.rDP, (unsigned)cpu.Regs.rE);
			}
		}

		for (size_t i = 0; i < touched.size(); i++)
			memory[touched[i]] = 0;
	}

	std::printf("CPU tests: %u, failures: %u\n", tests, failures);
	std::free(memory);
	return failures ? 1 : 0;
}
