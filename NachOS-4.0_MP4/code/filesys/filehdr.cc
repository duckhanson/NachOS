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
	memset(dataSectors, -1, sizeof(dataSectors));
	memset(sectorL0, -1, sizeof(sectorL0));
	memset(sectorL1, -1, sizeof(sectorL1));
	memset(sectorL2, -1, sizeof(sectorL2));
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

	int k = 0;
	size_t i, j, l, m;
	for (i = 0; i < NumDirect; i++)
	{
		sectorL0[i] = freeMap->FindAndSet();
		// since we checked that there was enough free space,
		// we expect this to succeed
		ASSERT(sectorL0[i] >= 0);
		for (j = 0; j < NumLevelOne; j++)
		{
			sectorL1[i][j] = freeMap->FindAndSet();
			ASSERT(sectorL1[i][j] >= 0);
			for (l = 0; l < NumLevelTwo; l++)
			{
				sectorL2[i][j][l] = freeMap->FindAndSet();
				ASSERT(sectorL2[i][j][l] >= 0);
				for (m = 0; m < NumLevelThree; m++)
				{
					dataSectors[i][j][l][m] = freeMap->FindAndSet();
					ASSERT(dataSectors[i][j][l][m] >= 0);
					k++;
					if (k == numSectors)
						return TRUE;
				}
			}
		}
	}
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
	int k = 0;
	size_t i, j, l, m;
	for (i = 0; i < NumDirect; i++)
	{
		for (j = 0; j < NumActualSectors; j++)
		{
			for (l = 0; l < NumActualSectors; l++)
			{
				for (m = 0; m < NumActualSectors; m++)
				{
					ASSERT(freeMap->Test((int)dataSectors[i][j][l][m])); // ought to be marked!
					freeMap->Clear((int)dataSectors[i][j][l][m]);
					k++;
					if (k == numSectors)
						return;
				}
			}
		}
	}
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
	char hdrbuf[SectorSize];
	ASSERT(sector >= 0 && sector < SectorSize);
	kernel->synchDisk->ReadSector(sector, (char *)hdrbuf);

	/*
		MP4 Hint:
		After you add some in-core informations, you will need to rebuild the header's structure
	*/
	size_t offset = 0;
	// metadata of the file
	memcpy(&numBytes, hdrbuf + offset, sizeof(int));
	offset += sizeof(int);
	memcpy(&numSectors, hdrbuf + offset, sizeof(int));
	offset += sizeof(int);
	size_t i, j, l;
	for (i = 0; i < NumDirect; i++) {
		memcpy(&sectorL0[i], hdrbuf + offset, sizeof(int));
		// offset += sizeof(int);
	}
	// indexed block
	for ( i = 0; i < NumDirect; i++)
	{
		if (sectorL0[i] != -1) {
			kernel->synchDisk->ReadSector(sectorL0[i], (char *)sectorL1[i]);
		}
		for ( j = 0; j < NumLevelOne; j++)
		{
			if (sectorL1[i][j] != -1) {
				kernel->synchDisk->ReadSector(sectorL1[i][j], (char *)sectorL2[i][j]);
			}
			for ( l = 0; l < NumLevelTwo; l++)
			{
				if (sectorL2[i][j][l] != -1) {
					kernel->synchDisk->ReadSector(sectorL2[i][j][l], (char *)dataSectors[i][j][l]);
				} else {
					return;
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
	char hdrbuf[SectorSize];
	size_t offset = 0;
	// metadata of the file
	memcpy(hdrbuf + offset, &numBytes, sizeof(int));
	offset += sizeof(int);
	memcpy(hdrbuf + offset, &numSectors, sizeof(int));
	offset += sizeof(int);

	// int k = 0;
	// indexed block
	memcpy(hdrbuf + offset, sectorL0, sizeof(sectorL0));
	kernel->synchDisk->WriteSector(sector, (char *)hdrbuf);
	size_t i, j, l;
	for (i = 0; i < NumDirect; i++)
	{
		if (sectorL0[i] != -1) {
			kernel->synchDisk->WriteSector(sectorL0[i], (char *)sectorL1[i]);
		}
		for (j = 0; j < NumLevelOne; j++)
		{
			if (sectorL1[i][j] != -1) {
				kernel->synchDisk->WriteSector(sectorL1[i][j], (char *)sectorL2[i][j]);
			}
			for (l = 0; l < NumLevelTwo; l++)
			{
				if (sectorL2[i][j][l] != -1) {
					kernel->synchDisk->WriteSector(sectorL2[i][j][l], (char *)dataSectors[i][j][l]);
				} else {
					return;
				}
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
	int NumToRealSector = (offset / SectorSize) % NumActualSectors;
	int NumL2 = ((offset / SectorSize) / NumActualSectors) % NumLevelTwo;
	int NumL1 = (((offset / SectorSize) / NumActualSectors) / NumLevelTwo) % NumLevelOne;
	int NumL0 = (((offset / SectorSize) / NumActualSectors) / NumLevelTwo) / NumLevelOne;
	return (dataSectors[NumL0][NumL1][NumL2][NumToRealSector]);
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
	// int i, j, k;
	// char *data = new char[SectorSize];

	printf("FileHeader contents.  File size: %d.  File blocks:", numBytes);
	for (size_t i = 0; i < NumDirect; i++) {
		if (i % 5 == 0)
			printf("\n");
		printf("%d ", sectorL0[i]);
	}
	

	// printf("%d ", dataSectors[i]);
	// printf("\nFile contents:\n");
	// for (i = k = 0; i < numSectors; i++)
	// {
	// 	kernel->synchDisk->ReadSector(dataSectors[i], data);
	// 	for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
	// 	{
	// 		if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
	// 			printf("%c", data[j]);
	// 		else
	// 			printf("\\%x", (unsigned char)data[j]);
	// 	}
	// 	printf("\n");
	// }
	// delete[] data;
}
