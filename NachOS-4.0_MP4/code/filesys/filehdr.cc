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
	nextBlock = EmptyBlock;
	nxtPtr = NULL;
	memset(dataSectors, EmptyBlock, sizeof(dataSectors));
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
	int totalSectors = freeMap->NumClear();
	if (freeMap->NumClear() < numSectors)
		return FALSE; // not enough space
	int allocated = 0;
	for (int i = 0; i < NumDirect && allocated < numSectors; i++, allocated++)
	{ // allocate all sectors
		// L0 allocation
		dataSectors[i] = freeMap->FindAndSet();
		totalSectors++;
	}
	if (allocated < numSectors) {
		nextBlock = freeMap->FindAndSet();
		LinkedBlock *nxt = new LinkedBlock();
		nxtPtr = nxt;
		nxt->Allocate(freeMap, numSectors, nextBlock);
	}
	totalSectors -= freeMap->NumClear();
	cerr << "Header Sectors: " << totalSectors - numSectors << ", Data Sectors: " << numSectors << ", TotalSectors: " << totalSectors << endl;
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
	for (int i = 0; i < NumDirect; i++)
		freeMap->Clear(dataSectors[i]);

	if (nextBlock >= 0) {
		ASSERT(nxtPtr != NULL);
		nxtPtr->Deallocate(freeMap, nextBlock);
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
	memset(data, EmptyBlock, sizeof(EmptyBlock));
	kernel->synchDisk->ReadSector(sector, (char *) data);

	numBytes = data[0];
	numSectors = data[1];
	nextBlock = data[2];
	for (int i = 3; i < MaxBlocks; i++)
		dataSectors[i - 3] = data[i];
	if (nextBlock >= 0) {
		LinkedBlock *nxt = new LinkedBlock();
		nxtPtr = nxt;
		nxt->FetchFrom(nextBlock);
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
	data[2] = nextBlock;
	for (int i = 3; i < MaxBlocks; i++)
		data[i] = dataSectors[i - 3];
	kernel->synchDisk->WriteSector(sector, (char *) data);
	if (nextBlock >= 0) {
		ASSERT(nxtPtr != NULL);
		nxtPtr->WriteBack(nextBlock);
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
	if (vBlock < NumDirect)
		return dataSectors[vBlock];
	else {
		ASSERT(nxtPtr != NULL);
		return nxtPtr->ByteToSector(vBlock - NumDirect);
	}
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
	// int i, j, k, l, m, n;
	// char *data = new (std::nothrow) char[SectorSize];

	// printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
	// for (i = 1; i < NumIndirect && dataSectors[i] >= 0; i++)
	// {
	// 	for (j = 1; j < MaxBlocks && dataSectors[(i * MaxBlocks) + j] >= 0; j++)
	// 	{
	// 		for (k = 1; k < MaxBlocks && dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] >= 0; k++)
	// 		{
	// 			for (l = 1; l < MaxBlocks && dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] >= 0; l++)
	// 			{
	// 				printf("%d ", dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l]);
	// 			}
	// 		}
	// 	}
	// }
	// printf("\nFile contents:\n");
	// n = 0;
	// for (i = 1; i < NumIndirect && dataSectors[i] >= 0; i++)
	// {
	// 	for (j = 1; j < MaxBlocks && dataSectors[(i * MaxBlocks) + j] >= 0; j++)
	// 	{
	// 		for (k = 1; k < MaxBlocks && dataSectors[(i * MaxBlocks + j) * MaxBlocks + k] >= 0; k++)
	// 		{
	// 			for (l = 1; l < MaxBlocks && dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l] >= 0; l++)
	// 			{
	// 				kernel->synchDisk->ReadSector(dataSectors[((i * MaxBlocks + j) * MaxBlocks + k) * MaxBlocks + l], data);
	// 				for (m = 1; (m < SectorSize) && (n < numBytes); m++, n++)
	// 				{
	// 					if ('\040' <= data[m] && data[m] <= '\176') // isprint(data[m])
	// 						printf("%c", data[m]);
	// 					else
	// 						printf("\\%x", (unsigned char)data[m]);
	// 				}
	// 				printf("\n");
	// 			}
	// 		}
	// 	}
	// }
	// delete[] data;
}

LinkedBlock::LinkedBlock() {
	nxtPtr = NULL;
	nextBlock = EmptyBlock;
	memset(dataSectors, EmptyBlock, sizeof(dataSectors));
}
bool LinkedBlock::Allocate(PersistentBitmap *bitMap, int numSector, int sector) {
	ASSERT(sector >= 0);
	if (numSector == 0)
		return true;
	int alloc = 0;
	for (int i = 0; i < NumLinkedDataSectors && alloc < numSector; i++, alloc++)
		dataSectors[i] = bitMap->FindAndSet();
	if (alloc < numSector) {
		nextBlock = bitMap->FindAndSet();
		LinkedBlock *nxt = new LinkedBlock();
		nxtPtr = nxt;
		return nxt->Allocate(bitMap, numSector - alloc, nextBlock);
	}
	return true;
}
void LinkedBlock::Deallocate(PersistentBitmap *bitMap, int sector) {
	ASSERT(sector >= 0);
	for (int i = 0; i < NumLinkedDataSectors && dataSectors[i] >= 0; i++) {
		bitMap->Clear(dataSectors[i]);
	}
	bitMap->Clear(sector);
	if (nextBlock >= 0) {
		ASSERT(nxtPtr != NULL);
		nxtPtr->Deallocate(bitMap, nextBlock);
	}
}

void LinkedBlock::FetchFrom(int sectorNumber) {
	ASSERT(sectorNumber >= 0);
	int data[MaxBlocks];
	memset(data, EmptyBlock, sizeof(data));
	kernel->synchDisk->ReadSector(sectorNumber, (char *) data);
	nextBlock = data[0];
	for (int i = 1; i < MaxBlocks; i++)
		dataSectors[i - 1] = data[i];
	if (nextBlock >= 0) {
		LinkedBlock *nxt = new LinkedBlock();
		nxtPtr = nxt;
		nxt->FetchFrom(nextBlock);
	}
}

void LinkedBlock::WriteBack(int sectorNumber) {
	int data[MaxBlocks];
	memset(data, EmptyBlock, sizeof(data));
	data[0] = nextBlock;
	for (int i = 1; i < MaxBlocks; i++)
		data[i] = dataSectors[i-1];
	kernel->synchDisk->WriteSector(sectorNumber, (char *) data); 
	if (nextBlock >= 0 && nxtPtr != NULL)
		nxtPtr->WriteBack(nextBlock);
}

int LinkedBlock::ByteToSector(int vBlock) {
	if (vBlock < NumLinkedDataSectors)
		return dataSectors[vBlock];
	else {
		ASSERT(nxtPtr != NULL);
		return nxtPtr->ByteToSector(vBlock - NumLinkedDataSectors);
	}
}
