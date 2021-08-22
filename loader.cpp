#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <assert.h>

#define NOMINMAX
#include <windows.h>

#include "exeflat.h"

size_t DecryptLE(const char* path, uint8_t*& buf)
{
	std::basic_ifstream<uint8_t> file(path, std::ios::in | std::ios::binary | std::ios::ate);
	if(!file.is_open())
		return 0;

	static size_t PrologueSize = 0xF424;
	size_t fileSize = static_cast<size_t>(file.tellg());
	if(fileSize < PrologueSize)
		return 0;

	buf = new uint8_t[fileSize];
	if(!buf)
		return 0;

	file.seekg(0, std::ios::beg);
	file.read(buf, fileSize);

	auto Decrypt = [](uint8_t* buf, size_t from, size_t to)
	{
		static uint8_t XorTable[18] = { 0x7A, 0x52, 0x2E, 0x5D, 0x7A, 0x59, 0x49, 0x20, 0x77, 0x0E, 0x23, 0x43, 0x38, 0x31, 0x32, 0x79, 0x01, 0x00 };

		size_t readOffset = 0, offsetKey = 0;
		uint8_t keyIndex = 0;
		for(size_t i = from; i < to; ++i)
		{
			buf[i] = (buf[i] ^ XorTable[keyIndex]) ^ static_cast<uint16_t>(offsetKey);

			if(++keyIndex == 16)
			{
				readOffset += 16;
				offsetKey = (readOffset & 0xFF) ^ ((readOffset & 0x0F00) >> 8);
				keyIndex = 0;
			}
		}
	};

	static uint16_t HeaderBytes1[14] =
	{
		0x5A4D, 0x0024, 0x007B, 0x004E, 0x0020, 0x0001, 0xFFFF, 0x0121, 0x0340, 0x44EC, 0x2384, 0x01BD, 0x001E, 0x0000
	};

	static uint16_t HeaderBytes2[79] =
	{
		0x0020, 0x0000, 0x0028, 0x0000, 0x098C, 0x0000, 0x0994, 0x0000, 0x09A0, 0x0000, 0x09A6, 0x0000, 0x09AA, 0x0000, 0x09AE, 0x0000,
		0x09B4, 0x0000, 0x09B6, 0x0000, 0x09F1, 0x0000, 0x01BD, 0x0348, 0x01BD, 0x0365, 0x01BD, 0x0411, 0x01BD, 0x044C, 0x01BD, 0x0494,
		0x01BD, 0x04E1, 0x01BD, 0x05AD, 0x01BD, 0x05B2, 0x01BD, 0x066A, 0x01BD, 0x069A, 0x01BD, 0x0772, 0x01BD, 0x07BA, 0x01BD, 0x07EA,
		0x01BD, 0x093B, 0x01BD, 0x094D, 0x01BD, 0x099E, 0x01BD, 0x09DA, 0x01BD, 0x09F3, 0x01BD, 0x0A3A, 0x01BD, 0x0A61, 0x01BD, 0x0A87,
		0x01BD, 0x0AA2, 0x01BD, 0x0AE5, 0x01BD, 0x0B15, 0x01BD, 0x0B6F, 0x01BD, 0x0B95, 0x01BD, 0x0DBD, 0x01BD, 0x02B8, 0x07DF
	};

	memcpy(buf, HeaderBytes1, sizeof(HeaderBytes1));
	memcpy(buf + 0x20, HeaderBytes2, sizeof(HeaderBytes2));

	Decrypt(buf, 0x200, PrologueSize);
	Decrypt(buf, PrologueSize, fileSize);
	return fileSize;
}

size_t FindLE(uint8_t* buf, uint32_t& dataPagesOffset)
{
	if(buf[0] != 'M' || buf[1] != 'Z')
		return 0;

	uint8_t* beg = buf;
	dataPagesOffset = 0;
	do
	{
		bool mz = buf[0] == 'M' && buf[1] == 'Z';
		bool bw = buf[0] == 'B' && buf[1] == 'W';
		if(!mz && !bw)
		{
			for(size_t i = 0; i < 0x1000; ++i, ++buf, ++dataPagesOffset)
			{
				if(buf[0] == 'L' && buf[1] == 'E')
					return buf - beg;
			}

			return 0;
		}

		uint16_t numWholePages, lastPageBytes;
		memcpy(&numWholePages, buf + 4, sizeof(uint16_t));
		memcpy(&lastPageBytes, buf + 2, sizeof(uint16_t));

		uint32_t execSize = ((numWholePages << 9) + lastPageBytes);
		if(mz)
			execSize -= 0x200;

		buf += execSize;
		dataPagesOffset = execSize;
	}
	while(true);
}

uint8_t* LoadLE(uint8_t* le, size_t& virtualSize)
{
	const os2_flat_header& hdr = *reinterpret_cast<os2_flat_header*>(le);

	virtualSize = 0;
	for(uint32_t i = 0; i < hdr.num_objects; ++i)
	{
		object_record& obj = reinterpret_cast<object_record*>(le + hdr.objtab_off)[i];
		uint32_t end = obj.addr + obj.size;
		if(end > virtualSize)
			virtualSize = end;
	}

	uint8_t* pages = new uint8_t[virtualSize];
	uint8_t** pagePtrs = new uint8_t*[hdr.num_pages];
	memset(pages, 0, virtualSize);
	memset(pagePtrs, 0, hdr.num_pages * sizeof(uint8_t*));
	for(uint32_t i = 0, p = 0; i < hdr.num_objects; ++i)
	{
		object_record obj = reinterpret_cast<object_record*>(le + hdr.objtab_off)[i];

		uint8_t* pagePtr = pages + obj.addr;
		for(uint32_t j = 0; j < obj.mapsize; ++j)
		{
			le_map_entry entry = reinterpret_cast<le_map_entry*>(le + hdr.objmap_off)[obj.mapidx + j - 1];

			uint32_t offset = entry.page_num[2] + (entry.page_num[1] << 8) + (static_cast<uint32_t>(entry.page_num[0]) << 16);
			if(offset)
				offset = hdr.page_off + ((offset - 1) * hdr.page_size);

			memcpy(pagePtr, le + offset, hdr.page_size);
			pagePtrs[p++] = pagePtr;
			pagePtr += hdr.page_size;
		}
	}

	uint8_t* fixrecs = le + hdr.fixrec_off;
	for(uint32_t i = 0; i < hdr.num_pages; ++i)
	{
		uint8_t* start = fixrecs + reinterpret_cast<uint32_t*>(le + hdr.fixpage_off)[i];
		uint8_t* end = fixrecs + reinterpret_cast<uint32_t*>(le + hdr.fixpage_off)[i + 1];
		uint8_t* page = pagePtrs[i];
		for(uint8_t* off = start; off < end; )
		{
			uint8_t source = *(off++);
			assert((source & OSF_SOURCE_LIST) == 0);

			uint8_t flags = *(off++);
			assert(flags == OSF_TARGET_OFF || flags == OSF_INTERNAL_REF);

			uint16_t src_off = 0;
			memcpy(&src_off, off, sizeof(uint16_t));
			off += sizeof(uint16_t);

			uint8_t obj_idx = *(off++);
			object_record obj = reinterpret_cast<object_record*>(le + hdr.objtab_off)[obj_idx - 1];

			uint32_t trg_off;
			if(flags == OSF_TARGET_OFF)
			{
				memcpy(&trg_off, off, sizeof(uint32_t));
				off += sizeof(uint32_t);
			}
			else
			{
				uint16_t trg_off_16;
				memcpy(&trg_off_16, off, sizeof(uint16_t));
				trg_off = trg_off_16;
				off += sizeof(uint16_t);
			}

			if((source == 7 || source == 8) && src_off > hdr.page_size)
				continue;

			switch(source)
			{
				case 7: // 32-bit offset
				{
					uint32_t trg_val = reinterpret_cast<uint32_t>(pages) + obj.addr + trg_off;
					memcpy(&page[src_off], &trg_val, sizeof(uint32_t));
					break;
				}

				case 8: // 32-bit relative
				{
					uint32_t self_rel_trg = reinterpret_cast<uint32_t>(&page[src_off]) + trg_off;
					memcpy(&page[src_off], &self_rel_trg, sizeof(uint32_t));
					break;
				}

				case 5:
				case 19:
					break;

				default:
				{
					printf("%d\n", source);
					DebugBreak();
					break;
				}
			}
		}
	}

	delete[] pagePtrs;
	return pages;
}

void ProtectLE(uint8_t* le, uint8_t* pages)
{
	const os2_flat_header& hdr = *reinterpret_cast<os2_flat_header*>(le);

	for(uint32_t i = 0; i < hdr.num_objects; ++i)
	{
		object_record obj = reinterpret_cast<object_record*>(le + hdr.objtab_off)[i];

		bool readable = obj.flags & OBJ_READABLE;
		bool writeable = obj.flags & OBJ_WRITEABLE;
		bool executable = obj.flags & OBJ_EXECUTABLE;

		DWORD prot;
		if(!readable && !writeable && executable)
			prot = PAGE_EXECUTE;
		else if(readable && !writeable && executable)
			prot = PAGE_EXECUTE_READ;
		else if(readable && writeable && executable)
			prot = PAGE_EXECUTE_READWRITE;
		else if(readable && writeable && !executable)
			prot = PAGE_READWRITE;
		else
			prot = PAGE_READONLY;

		DWORD oldProt;
		VirtualProtect(pages + obj.addr, obj.size, prot, &oldProt);
	}
}

uintptr_t LoadSQ(const char* path, size_t& virtualSize, void(*patchFunc)(uintptr_t))
{
    uint8_t* exeBuf;
    size_t exeSize = DecryptLE(path, exeBuf);
    if(!exeSize)
        return 0;

    size_t dataPagesOffset;
    size_t linExeOffset = FindLE(exeBuf, dataPagesOffset);
    if(!linExeOffset)
	{
		delete[] exeBuf;
        return 0;
	}

    os2_flat_header* header = reinterpret_cast<os2_flat_header*>(exeBuf + linExeOffset);
    header->page_off -= dataPagesOffset;

	uint8_t* game = LoadLE(exeBuf + linExeOffset, virtualSize);
	patchFunc(reinterpret_cast<uintptr_t>(game));
	ProtectLE(exeBuf + linExeOffset, game);

    memset(exeBuf, 0, exeSize);
    delete[] exeBuf;

	return reinterpret_cast<uintptr_t>(game);
}
