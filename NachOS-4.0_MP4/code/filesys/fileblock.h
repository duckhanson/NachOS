
// #ifdef CHANGED

#ifndef FILE_BLOCK_H
#define FILE_BLOCK_H

#include "disk.h"
#include "pbitmap.h"

#define MAX_BLOCKS (int)(SectorSize / sizeof(int))
#define EMPTY_BLOCK -1

class IndirectBlock
{
public:
    IndirectBlock();
    virtual int Allocate(PersistentBitmap *bitMap, int numSectors) = 0; // Initialize a indirect block

    virtual void Deallocate(PersistentBitmap *bitMap) = 0; // De-allocate this file's
                                                           //  data blocks

    //  back to disk

    virtual int ByteToSector(int offset) = 0; // Convert a byte offset into the file
                                              // to the disk sector containing
                                              // the byte

    void FetchFrom(int sectorNumber); // Initialize file header from disk
    void WriteBack(int sectorNumber); // Write modifications to file header
    // int FileLength();			// Return the length of the file
    // in bytes
    int dataSectors[MAX_BLOCKS];
};

class SingleIndirectBlock : public IndirectBlock
{
public:
    SingleIndirectBlock() : IndirectBlock() {}

    virtual int Allocate(PersistentBitmap *bitMap, int numSectors); // Initialize a indirect block

    virtual void Deallocate(PersistentBitmap *bitMap); // De-allocate this file's
                                                                //  data blocks

    virtual int ByteToSector(int offset); // Convert a byte offset into the file
                                                   // to the disk sector containing
                                                   // the byte
};

class DoublyIndirectBlock : public IndirectBlock
{
public:
    DoublyIndirectBlock() : IndirectBlock() {}

    virtual int Allocate(PersistentBitmap *bitMap, int numSectors); // Initialize a indirect block

    virtual void Deallocate(PersistentBitmap *bitMap); // De-allocate this file's
                                                                //  data blocks
    virtual int ByteToSector(int offset);              // Convert a byte offset into the file
                                                                // to the disk sector containing
                                                                // the byte
};

class TripleIndirectBlock : public IndirectBlock
{
public:
    TripleIndirectBlock() : IndirectBlock() {}

    virtual int Allocate(PersistentBitmap *bitMap, int numSectors); // Initialize a indirect block

    virtual void Deallocate(PersistentBitmap *bitMap); // De-allocate this file's
                                                                //  data blocks
    virtual int ByteToSector(int offset);              // Convert a byte offset into the file
                                                                // to the disk sector containing
                                                                // the byte
};

#endif // FILE_BLOCK_H

// #endif //CHANGED