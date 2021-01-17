// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "filehdr.h"
#include "debug.h"
#include "synchdisk.h"
#include "main.h"

#ifndef MaxBlocks
#define MaxBlocks (int)(SectorSize / sizeof(int))
#endif
#define idxL0 i
#define idxL1 i * MaxBlocks + j
#define idxL2 (i * MaxBlocks + j) * MaxBlocks + k
#define idxL3 ((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l
//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader()
{
	numBytes = -1;
	numSectors = -1;
	memset(dataSectors, EmptyBlock, sizeof(dataSectors));
	memset(virDataSectors, EmptyBlock, sizeof(virDataSectors));
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader()
{
	// nothing to do now
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{
	numBytes = fileSize;
	numSectors = divRoundUp(fileSize, SectorSize);
	if (freeMap->NumClear() < numSectors)
		return FALSE; // not enough space
	int allocated = 0;
	for (int i = 1; i < NumIndirect && allocated < numSectors; i++)
	{ // allocate all sectors
		// L0 allocation
		dataSectors[i] = freeMap->FindAndSet();
		ASSERT(dataSectors[i] != EmptyBlock);
		for (int j = 1; j < MaxBlocks && allocated < numSectors; j++)
		{
			dataSectors[(i * MaxBlocks) + j] = freeMap->FindAndSet();
			ASSERT(dataSectors[(i * MaxBlocks) + j] != EmptyBlock);
			for (int k = 1; k < MaxBlocks && allocated < numSectors; k++)
			{
				dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] = freeMap->FindAndSet();
				ASSERT(dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] != EmptyBlock);
				for (int l = 1; l < MaxBlocks && allocated < numSectors; l++)
				{
					dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] = freeMap->FindAndSet();
					virDataSectors[++allocated] = ((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l;
					ASSERT(dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] != EmptyBlock);
				}
			}
		}
	}
	DEBUG('e', "file header allocated\n");
	return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(PersistentBitmap *freeMap)
{
	DEBUG('r', "beginning filehdr deallocation\n");
	int tblock[MaxBlocks];
	int deallocate = 0;
	for (int i = 1; i < NumIndirect && deallocate < numSectors && dataSectors[i] != EmptyBlock; i++)
	{ // deallocate all sectors

		kernel->synchDisk->ReadSector(dataSectors[i], (char *)tblock);
		for (int j = 1; j < MaxBlocks; j++)
			dataSectors[(i * MaxBlocks) + j] = tblock[j];

		for (int j = 1; j < MaxBlocks && deallocate < numSectors && dataSectors[(i * MaxBlocks) + j] != EmptyBlock; j++)
		{
			kernel->synchDisk->ReadSector(dataSectors[(i * MaxBlocks) + j], (char *)tblock);
			for (int k = 1; k < MaxBlocks; k++)
				dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] = tblock[k];
			for (int k = 1; k < MaxBlocks && deallocate < numSectors && dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] != EmptyBlock; k++)
			{
				kernel->synchDisk->ReadSector(dataSectors[(i * MaxBlocks + j) * MaxBlocks + k], (char *)tblock);
				for (int l = 1; l < MaxBlocks; l++)
					dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] = tblock[l];
				for (int l = 1; l < MaxBlocks && deallocate < numSectors && dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] != EmptyBlock; l++)
				{
					freeMap->Clear(dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l]);
					deallocate++;
				}
				freeMap->Clear(dataSectors[(i * MaxBlocks + j) * MaxBlocks + k]);
			}
			freeMap->Clear(dataSectors[(i * MaxBlocks) + j]);
		}
		freeMap->Clear(dataSectors[i]);
	}
	DEBUG('r', "finished filehdr deallocation\n");
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
	int data[MaxBlocks];
	memset(dataSectors, EmptyBlock, sizeof(dataSectors));
	memset(virDataSectors, EmptyBlock, sizeof(virDataSectors));
	memset(data, EmptyBlock, sizeof(data));
	kernel->synchDisk->ReadSector(sector, (char *)data);
	numBytes = data[0];
	numSectors = data[1];
	for (int i = 1; i < NumIndirect; i++)
		dataSectors[i] = data[i];

	int virIdx = 0;
	for (int i = 1; i < NumIndirect && dataSectors[i] != EmptyBlock; i++)
	{
		memset(data, EmptyBlock, sizeof(data));
		kernel->synchDisk->ReadSector(dataSectors[i], (char *)data);
		for (int j = 1; j < MaxBlocks; j++)
			dataSectors[i * MaxBlocks + j] = data[j];
		for (int j = 1; j < MaxBlocks && dataSectors[i * MaxBlocks + j] != EmptyBlock; j++)
		{
			memset(data, EmptyBlock, sizeof(data));
			kernel->synchDisk->ReadSector(dataSectors[i * MaxBlocks + j], (char *)data);
			for (int k = 1; k < MaxBlocks; k++)
				dataSectors[(i * MaxBlocks + j) + k] = data[k];
			for (int k = 1; k < MaxBlocks && dataSectors[(i * MaxBlocks + j) + k] != EmptyBlock; k++)
			{
				memset(data, EmptyBlock, sizeof(data));
				kernel->synchDisk->ReadSector(dataSectors[(i * MaxBlocks + j) + k], (char *)data);
				for (int l = 1; l < MaxBlocks; l++)
				{
					dataSectors[((i * MaxBlocks + j) + k) + l] = data[l];
					virDataSectors[++virIdx] = ((i * MaxBlocks + j) + k) + l;
				}
			}
		}
	}
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector)
{
	/*
		MP4 Hint:
		After you add some in-core informations, you may not want to write all fields into disk.
		Use this instead:
		char buf[SectorSize];
		memcpy(buf + offset, &dataToBeWritten, sizeof(dataToBeWritten));
		...
	*/
	int data[MaxBlocks];
	memset(data, EmptyBlock, sizeof(data));
	data[0] = numBytes;
	data[1] = numSectors;
	for (int i = 1; i < NumIndirect; i++)
	{
		data[i + 2] = dataSectors[i];
	}
	kernel->synchDisk->WriteSector(sector, (char *)data);
	
	for (int i = 1; i < NumIndirect && dataSectors[i] != EmptyBlock; i++)
	{
		memset(data, EmptyBlock, sizeof(data));
		for (int j = 1; j < MaxBlocks; j++)
		{
			data[j] = dataSectors[(i * MaxBlocks) + j];
		}
		kernel->synchDisk->WriteSector(dataSectors[i], (char *)data);

		for (int j = 1; j < MaxBlocks && dataSectors[i * MaxBlocks + j] != EmptyBlock; j++)
		{
			memset(data, EmptyBlock, sizeof(data));
			for (int k = 1; k < MaxBlocks; k++)
			{
				data[k] = dataSectors[(i * MaxBlocks + j) * MaxBlocks + k];
			}
			kernel->synchDisk->WriteSector(dataSectors[(i * MaxBlocks) + j], (char *)data);

			for (int k = 1; k < MaxBlocks && dataSectors[(i * MaxBlocks + j) + k] != EmptyBlock; k++)
			{
				memset(data, EmptyBlock, sizeof(data));
				for (int l = 1; l < MaxBlocks; l++)
				{
					data[l] = dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l];
				}
				kernel->synchDisk->WriteSector(dataSectors[(i * MaxBlocks + j) + k], (char *)data);
			}
		}
	}
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset)
{
	int vBlock = offset / SectorSize;

	return dataSectors[virDataSectors[vBlock]];
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength()
{
	return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void FileHeader::Print()
{
	int i, j, k, l, m, n;
	char *data = new (std::nothrow) char[SectorSize];

	printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
	for (i = 1; i < NumIndirect && dataSectors[i] != EmptyBlock; i++)
	{
		for (j = 1; j < MaxBlocks && dataSectors[(i * MaxBlocks) + j] != EmptyBlock; j++)
		{
			for (k = 1; k < MaxBlocks && dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] != EmptyBlock; k++)
			{
				for (l = 1; l < MaxBlocks && dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] != EmptyBlock; l++)
				{
					printf("%d ", dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l]);
				}
			}
		}
	}
	printf("\nFile contents:\n");
	n = 0;
	for (i = 1; i < NumIndirect && dataSectors[i] != EmptyBlock; i++)
	{
		for (j = 1; j < MaxBlocks && dataSectors[(i * MaxBlocks) + j] != EmptyBlock; j++)
		{
			for (k = 1; k < MaxBlocks && dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] != EmptyBlock; k++)
			{
				for (l = 1; l < MaxBlocks && dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] != EmptyBlock; l++)
				{
					kernel->synchDisk->ReadSector(dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l], data);
					for (m = 1; (m < SectorSize) && (n < numBytes); m++, n++)
					{
						if ('\040' <= data[m] && data[m] <= '\176') // isprint(data[m])
							printf("%c", data[m]);
						else
							printf("\\%x", (unsigned char)data[m]);
					}
					printf("\n");
				}
			}
		}
	}
	delete[] data;
}
