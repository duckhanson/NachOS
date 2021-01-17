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
	memset(dataL0Sectors, EmptyBlock, sizeof(dataL0Sectors));
	memset(dataL1Sectors, EmptyBlock, sizeof(dataL1Sectors));
	memset(dataL2Sectors, EmptyBlock, sizeof(dataL2Sectors));
	memset(dataL3Sectors, EmptyBlock, sizeof(dataL3Sectors));
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
	for (int i = 0; i < NumIndirect && allocated < numSectors; i++)
	{ // allocate all sectors
		dataL0Sectors[i] = freeMap->FindAndSet();
		ASSERT(dataL0Sectors[i] >= 0);
		for (int j = 0; j < MaxBlocks && allocated < numSectors; j++)
		{
			dataL1Sectors[i][j] = freeMap->FindAndSet();
			ASSERT(dataL1Sectors[i][j] >= 0);
			for (int k = 0; k < MaxBlocks && allocated < numSectors; k++)
			{
				dataL2Sectors[i][j][k] = freeMap->FindAndSet();
				ASSERT(dataL2Sectors[i][j][k] >= 0);
				for (int l = 0; l < MaxBlocks && allocated < numSectors; l++)
				{
					dataL3Sectors[i][j][k][l] = freeMap->FindAndSet();
					ASSERT(dataL3Sectors[i][j][k][l] >= 0);
					allocated++;
				}
				kernel->synchDisk->WriteSector(dataL2Sectors[i][j][k], (char *)dataL3Sectors[i][j][k]);
			}
			kernel->synchDisk->WriteSector(dataL1Sectors[i][j], (char *)dataL2Sectors[i][j]);
		}
		kernel->synchDisk->WriteSector(dataL0Sectors[i], (char *)dataL1Sectors[i]);
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
	int deallocate = 0;
	for (int i = 0; i < NumIndirect && deallocate < numSectors; i++)
	{ // deallocate all sectors
		ASSERT(dataL0Sectors[i] >= 0);
		kernel->synchDisk->ReadSector(dataL0Sectors[i], (char*) dataL1Sectors[i]);
		for (int j = 0; j < MaxBlocks && deallocate < numSectors; j++)
		{
			ASSERT(dataL1Sectors[i][j] >= 0);
			kernel->synchDisk->ReadSector(dataL1Sectors[i][j], (char*) dataL2Sectors[i][j]);
			for (int k = 0; k < MaxBlocks && deallocate < numSectors; k++)
			{
				ASSERT(dataL2Sectors[i][j][k] >= 0);
				kernel->synchDisk->ReadSector(dataL2Sectors[i][j][k], (char*) dataL3Sectors[i][j][k]);
				for (int l = 0; l < MaxBlocks && deallocate < numSectors; l++)
				{
					ASSERT(dataL3Sectors[i][j][k][l] >= 0);
					freeMap->Clear(dataL3Sectors[i][j][k][l]);
					deallocate++;
				}
			}
		}
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
	kernel->synchDisk->ReadSector(sector, (char *)data);
	numBytes = data[0];
	numSectors = data[1];
	for (int i = 0; i < NumIndirect; i++) {
		dataL0Sectors[i] = data[i + 2];
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
	data[0] = numBytes;
	data[1] = numSectors;
	for (int i = 0; i < NumIndirect; i++)
	{
		dataL0Sectors[i] = data[i + 2];
	}
	kernel->synchDisk->WriteSector(sector, (char *)data);
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
	int NumL3 = vBlock % MaxBlocks;
	int NumL2 = (vBlock / MaxBlocks) % MaxBlocks;
	int NumL1 = ((vBlock / MaxBlocks) / MaxBlocks) % MaxBlocks;
	int NumL0 = ((vBlock / MaxBlocks) / MaxBlocks) / MaxBlocks;
	kernel->synchDisk->ReadSector(dataL0Sectors[NumL0], (char*) dataL1Sectors[NumL0]);
	kernel->synchDisk->ReadSector(dataL1Sectors[NumL0][NumL1], (char *)dataL2Sectors[NumL0][NumL1]);
	kernel->synchDisk->ReadSector(dataL2Sectors[NumL0][NumL1][NumL2], (char *)dataL3Sectors[NumL0][NumL1][NumL2]);

	return dataL3Sectors[NumL0][NumL1][NumL2][NumL3];
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
	int i, j, k;
	char *data = new (std::nothrow) char[SectorSize];

	printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
	for (i = 0; i < numSectors; i++)
		printf("%d ", dataL0Sectors[i]);
	printf("\nFile contents:\n");
	for (i = k = 0; i < numSectors; i++)
	{
		kernel->synchDisk->ReadSector(dataL0Sectors[i], data);
		for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
		{
			if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
				printf("%c", data[j]);
			else
				printf("\\%x", (unsigned char)data[j]);
		}
		printf("\n");
	}
	delete[] data;
}
