
// #ifdef CHANGED

#include "debug.h"
#include "fileblock.h"
#include "filehdr.h"
#include "synchdisk.h"
#include "main.h"
//############################################################################################################//
IndirectBlock::IndirectBlock()
{
    for (int i = 0; i < MAX_BLOCKS; ++i)
        dataSectors[i] = EMPTY_BLOCK;
}
void IndirectBlock::WriteBack(int sector)
{
    kernel->synchDisk->WriteSector(sector, (char *)this);
}

void IndirectBlock::FetchFrom(int sector)
{
    kernel->synchDisk->WriteSector(sector, (char *)this);
}

//############################################################################################################//
int SingleIndirectBlock::Allocate(PersistentBitmap *freeMap, int numSectors)
{ // Initialize a file header,
    DEBUG('e', "starting single indirect allocation\n");
    if (numSectors < 0)
        return -1;

    if (freeMap->NumClear() < numSectors) // failure if not enough free sectors on disk
        return -1;

    DEBUG('e', "enough space for single indirect allocation\n");
    int allocated = 0;
    for (int i = 0; i < MAX_BLOCKS && allocated < numSectors; ++i)
    { // allocate space for all blocks
        if (dataSectors[i] != EMPTY_BLOCK)
            continue;
        dataSectors[i] = freeMap->FindAndSet();
        ASSERT(dataSectors[i] != EMPTY_BLOCK);
        ++allocated;
    }

    DEBUG('e', "single indirect allocated\n");
    return allocated;
}

void SingleIndirectBlock::Deallocate(PersistentBitmap *freeMap)
{
    DEBUG('r', "beginning indirect block deallocation\n");
    for (int i = 0, sector; i < MAX_BLOCKS; ++i)
    { // deallocate all sectors
        sector = dataSectors[i];
        if (sector == EMPTY_BLOCK)
            continue;
        ASSERT(freeMap->Test(sector)); // assert that sector to be cleared is in use
        freeMap->Clear(sector);
    }
    DEBUG('r', "finished indirect block deallocation\n");
}

int SingleIndirectBlock::ByteToSector(int offset)
{
    int vBlock = offset / SectorSize;
    ASSERT(vBlock < MAX_BLOCKS); // assert that it is a valid virtual block
    int pBlock = dataSectors[vBlock];
    ASSERT(pBlock >= 0 && pBlock < NumSectors);
    return pBlock;
}

//############################################################################################################//

int DoublyIndirectBlock::Allocate(PersistentBitmap *freeMap, int numSectors)
{ // Initialize a file header,
    SingleIndirectBlock *iblock;

    DEBUG('e', "starting doublyindirect allocation\n");
    // printf("numSectors requested dblock allocation: %d\n", numSectors);
    if (numSectors < 0)
        return -1;
    if (freeMap->NumClear() < numSectors) // failure if not enough free sectors on disk
        return -1;

    DEBUG('e', "enough space for doublyindirect allocation\n");
    int allocated = 0;
    for (int i = 0; i < MAX_BLOCKS && allocated < numSectors; ++i)
    { // allocate space for all indirect blocks
        iblock = new (std::nothrow) SingleIndirectBlock();
        if (dataSectors[i] == EMPTY_BLOCK)
            dataSectors[i] = freeMap->FindAndSet(); // allocate block for indirect block
        else
            iblock->FetchFrom(dataSectors[i]);
        ASSERT(dataSectors[i] != EMPTY_BLOCK);
        int result = iblock->Allocate(freeMap, numSectors - allocated);
        ASSERT(result >= 0);
        iblock->WriteBack(dataSectors[i]); // write indirect block hdr back to disk
        allocated += result;
        delete iblock;
    }

    DEBUG('e', "doubly indirect block allocated\n");
    return allocated;
}

void DoublyIndirectBlock::Deallocate(PersistentBitmap *freeMap)
{
    DEBUG('r', "beginning doublyindirect deallocation\n");
    SingleIndirectBlock *iblock;
    for (int i = 0, sector; i < MAX_BLOCKS; ++i)
    { // deallocate all blocks
        sector = dataSectors[i];
        if (sector == EMPTY_BLOCK) // skip empty block
            continue;
        ASSERT(freeMap->Test(sector)); // assert that the sector we are deallocating is in use
        iblock = new (std::nothrow) SingleIndirectBlock();
        iblock->FetchFrom(sector);     // load up filehdr
        iblock->Deallocate(freeMap);   // deallocate filehdr
        ASSERT(freeMap->Test(sector)); // just to be sure nothing weird happened
        freeMap->Clear(sector);
        delete iblock;
    }
    DEBUG('r', "finished doubly indirect deallocation\n");
}

int DoublyIndirectBlock::ByteToSector(int offset)
{
    int vBlock = offset / SectorSize; // calc virtual block we want
    SingleIndirectBlock *iblock = new (std::nothrow) SingleIndirectBlock();
    iblock->FetchFrom(dataSectors[vBlock / MAX_BLOCKS]);                   // load up indirect block hdr that contains the virtual block we want
    int pBlock = iblock->ByteToSector((vBlock % MAX_BLOCKS) * SectorSize); // find the corresponding physical block
    delete iblock;
    // printf("doublyindirect ByteToSector: %d\n", pBlock);
    ASSERT(pBlock >= 0 && pBlock < NumSectors);
    return pBlock;
}

//############################################################################################################//
// TripleIndirectBlock::TripleIndirectBlock() {
// 	for(int i = 0; i < MAX_BLOCKS; ++i)
// 		dataSectors[i] = EMPTY_BLOCK;
// }

int TripleIndirectBlock::Allocate(PersistentBitmap *freeMap, int numSectors)
{ // Initialize a file header,
    // IndirectBlock *iblock;
    DoublyIndirectBlock *iblock;
    DEBUG('e', "starting tripleindirect allocation\n");
    // printf("numSectors requested dblock allocation: %d\n", numSectors);
    if (numSectors < 0)
        return -1;
    if (freeMap->NumClear() < numSectors) // failure if not enough free sectors on disk
        return -1;

    DEBUG('e', "enough space for tripleindirect allocation\n");
    int allocated = 0;
    for (int i = 0; i < MAX_BLOCKS && allocated < numSectors; ++i)
    { // allocate space for all indirect blocks
        iblock = new (std::nothrow) DoublyIndirectBlock();
        if (dataSectors[i] == EMPTY_BLOCK)
            dataSectors[i] = freeMap->FindAndSet(); // allocate block for indirect block
        else
            iblock->FetchFrom(dataSectors[i]);
        ASSERT(dataSectors[i] != EMPTY_BLOCK);
        int result = iblock->Allocate(freeMap, numSectors - allocated);
        ASSERT(result >= 0);
        iblock->WriteBack(dataSectors[i]); // write indirect block hdr back to disk
        allocated += result;
        delete iblock;
    }

    DEBUG('e', "doubly indirect block allocated\n");
    return allocated;
}

void TripleIndirectBlock::Deallocate(PersistentBitmap *freeMap)
{
    DEBUG('r', "beginning doublyindirect deallocation\n");
    // IndirectBlock *iblock;
    DoublyIndirectBlock *iblock;
    for (int i = 0, sector; i < MAX_BLOCKS; ++i)
    { // deallocate all blocks
        sector = dataSectors[i];
        if (sector == EMPTY_BLOCK) // skip empty block
            continue;
        ASSERT(freeMap->Test(sector)); // assert that the sector we are deallocating is in use
        iblock = new (std::nothrow) DoublyIndirectBlock();
        iblock->FetchFrom(sector);     // load up filehdr
        iblock->Deallocate(freeMap);   // deallocate filehdr
        ASSERT(freeMap->Test(sector)); // just to be sure nothing weird happened
        freeMap->Clear(sector);
        delete iblock;
    }
    DEBUG('r', "finished doubly indirect deallocation\n");
}

int TripleIndirectBlock::ByteToSector(int offset)
{
    int vBlock = offset / SectorSize; // calc virtual block we want
    DoublyIndirectBlock *iblock = new (std::nothrow) DoublyIndirectBlock();
    iblock->FetchFrom(dataSectors[vBlock / MAX_BLOCKS]);                   // load up indirect block hdr that contains the virtual block we want
    int pBlock = iblock->ByteToSector((vBlock % MAX_BLOCKS) * SectorSize); // find the corresponding physical block
    delete iblock;
    // printf("tripleindirect ByteToSector: %d\n", pBlock);
    ASSERT(pBlock >= 0 && pBlock < NumSectors);
    return pBlock;
}

// #endif