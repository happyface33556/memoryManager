//
// Created by Zachary Deptula on 3/11/22.
//

#include "MemoryManager.h"
#include <iostream>
#include <utility>
#include <iterator>
#include <vector>
#include <sstream>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <cstring>
using namespace std;

MemoryManager::MemoryManager(unsigned wordSize, function<int(int, void *)> allocator) {
    this->wordSize = wordSize;
    this->allocator = allocator;
};

MemoryManager::~MemoryManager() {
    shutdown();
};

void MemoryManager::initialize(size_t sizeInWords) {
    unsigned long numBytes = sizeInWords * wordSize;
    this->memLimit = numBytes;
    shutdown();
    if (sizeInWords <= 65535) {
        memory = new char[numBytes];
        Block initBlock(true, sizeInWords, 0, numBytes);
        blockList.push_front(initBlock);
        this->numHoles = 1;
        if (memory == nullptr) {
            cout << "Error Occurred." << endl;
            return;
        }
    }
    else {
        cout << "ERROR: TOO MANY WORDS" << endl;
    }
};

void MemoryManager::shutdown() {
    delete[] memory;
    memory = nullptr;
    blockList.clear();
};

void* MemoryManager::allocate(size_t sizeInBytes) {
    unsigned long long numWords = ceil(sizeInBytes/wordSize);
    if (blockList.size() == 0 && blockList.front().isFree) {
        Block block(false, numWords, 0, sizeInBytes);
        blockList.front().setOffset(numWords);
        blockList.front().setNumWords(blockList.front().numWords - numWords);
        blockList.push_front(block);
        char* newBlockLoc = memory;
        return newBlockLoc;
    }
    else {
        uint16_t* holeList = static_cast<uint16_t*>(getList());
        int offsetLocation = allocator((int) numWords, holeList);
        delete[] holeList;
        if (offsetLocation == -1) {
            return nullptr;
        }
        else {
            Block block(false, numWords, offsetLocation, sizeInBytes);
            list<Block>::iterator it;
            for (it = blockList.begin(); it != blockList.end(); it++) {
                if (it->offset == offsetLocation) {
                    blockList.insert(it, block);
                    break;
                }
            }
            it->setOffset(it->offset + numWords);
            if (it->numWords == numWords) {
                blockList.erase(it);
            }
            else {
                it->setNumWords(it->numWords - numWords);
            }
            char* newBlockLoc = memory + (wordSize * offsetLocation);
            return newBlockLoc;
        }
    }
};

void MemoryManager::free(void *address) {
    unsigned long byteOffset = (char*) address - memory;
    int offsetToFree = byteOffset/wordSize;
    list<Block>::iterator it;
    for (it = blockList.begin(); it != blockList.end(); it++) {
        if (it->offset == offsetToFree) {
            it->setIsFree(true);
            break;
        }
    }
    auto nextElem = next(it);
    int newBlockOffset = -1;
    int newBlockLength = it->numWords;
    auto prevElem = prev(it);
    if (it->offset == 0) {
        if (nextElem->isFree) {
            newBlockLength = newBlockLength + nextElem->numWords;
            blockList.erase(nextElem);
            it->numWords = newBlockLength;
            return;
        }
        else {
            return;
        }
    }
    if (((prevElem->isFree && it->offset != 0)) || (nextElem)->isFree && (((it)->offset + (it)->numWords) < memLimit)) {
        if ((prevElem)->isFree && ((it)->offset != 0)) {
            newBlockOffset = (prevElem)->offset;
            newBlockLength = newBlockLength + (prevElem)->numWords;
            if ((nextElem)->isFree) {
                newBlockLength = newBlockLength + (nextElem)->numWords;
                blockList.erase(prevElem);
                blockList.erase(it);
                (nextElem)->offset = newBlockOffset;
                (nextElem)->numWords = newBlockLength;
            }
            else {
                blockList.erase(prevElem);
                (it)->offset = newBlockOffset;
                (it)->numWords = newBlockLength;
            }
            return;
        }
        if ((nextElem)->isFree) {
            newBlockLength = newBlockLength + (nextElem)->numWords;
            blockList.erase(nextElem);
            (it)->numWords = newBlockLength;
        }
    }
};

void MemoryManager::setAllocator(function<int(int, void *)> allocator) {
    this->allocator = allocator;
};

int MemoryManager::dumpMemoryMap(char *filename) {
    int pfd;
    list<Block>::iterator it;
    string memMap;
    stringstream s;
    for (it = blockList.begin(); it != blockList.end(); it++) {
        if ((it)->isFree) {
            memMap.append("[");
            s << (it)->offset;
            memMap.append(s.str());
            s.str("");
            s.clear();
            memMap.append(", ");
            s << (it)->numWords;
            memMap.append(s.str());
            s.str("");
            s.clear();
            memMap.append("] - ");
        }
    }
    memMap = memMap.substr(0, (memMap.length() - 3));
    char buf[memMap.length() + 1];
    strcpy(buf, memMap.c_str());
    pfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (pfd == -1) {
        return -1;
    }
    else {
        write(pfd, buf, strlen(buf));
        close(pfd);
        return 0;
    }
};

void* MemoryManager::getList() {
    if (blockList.empty()) {
        return nullptr;
    }
    else {
        int numHoles = 0;
        list<Block>::iterator it;
        for (it = blockList.begin(); it != blockList.end(); it++) {
            if ((it)->isFree) {
                numHoles = numHoles + 1;
            }
        }
        int numElements = (numHoles * 2) + 1;
        //uint16_t* arr = (uint16_t*) malloc(numElements * sizeof(int));
        uint16_t* arr;
        arr = new uint16_t [numElements];
        arr[0] = numHoles;
        int position = 1;
        for (it = blockList.begin(); it != blockList.end(); it++) {
            if ((it)->isFree) {
                arr[position] = (int) (it)->offset;
                ++position;
                arr[position] = (int) (it)->numWords;
                ++position;
            }
        }
        return arr;
    }
};

void* MemoryManager::getBitmap() {
    list<Block>::iterator it;
    vector<string> result;
    for (it = blockList.begin(); it != blockList.end(); it++) {
        int numInputs = (uint8_t) (it)->numWords;
        if ((it)->isFree) {
            for (unsigned int i = 0; i < numInputs; i++) {
                result.push_back("0");
            }
        }
        else {
            for (unsigned int i = 0; i < numInputs; i++) {
                result.push_back("1");
            }
        }
    }
    int bitCount = 8;
    vector<string> byteResults;
    for (unsigned int i = 0; i < result.size();) {
        string byteString = "";
        for (unsigned int j = 0; j < bitCount; j++) {
            byteString.append(result[i]);
            i++;
        }
        byteResults.push_back(byteString);
    }
    stringstream stream;
    stream << hex << byteResults.size();
    uint8_t* arr = new uint8_t [byteResults.size() + 2];
    string res = stream.str();
    string single = "000";
    string doub = "00";
    string triple = "0";
    if (res.length() == 1) {
        res = single.append(res);
    }
    if (res.length() == 2) {
        res = doub.append(res);
    }
    if (res.length() == 3) {
        res = triple.append(res);
    }
    string high = res.substr(0,2);
    unsigned long long highByte;
    unsigned long long lowByte;
    string low = res.substr(2,2);
    stringstream s;
    s << high;
    s >> hex >> highByte;
    s.clear();
    s << low;
    s >> hex >> lowByte;
    int position = 2;
    arr[0] = lowByte;
    arr[1] = highByte;
    for (unsigned int i = 0; i < byteResults.size(); i++) {
        reverse(byteResults[i].begin(), byteResults[i].end());
        arr[position] = stoull(byteResults[i], 0, 2);
        position++;
    }
    return arr;
}
unsigned MemoryManager::getWordSize() {
    return wordSize;
}

void* MemoryManager::getMemoryStart() {
    return (uint64_t*) memory;
};

unsigned MemoryManager::getMemoryLimit() {
    return memLimit;
};

int MemoryManager::getNumHoles() {
    return numHoles;
}

//MEMORY ALLOCATION ALGORITHMS

int bestFit(int sizeInWords, void *list) {
    uint16_t* holeList = (uint16_t*) list;
    int size = (holeList[0] * 2) + 1;
    int bestSpace = 65537;
    int bestHoleOffset;
    for (unsigned int i = 1; i < size; i++) {
        if ((i % 2) == 0) {
            int holeLength = holeList[i];
            if (sizeInWords <= holeLength) {
                int space = holeLength - sizeInWords;
                if (space < bestSpace) {
                    bestSpace = space;
                    bestHoleOffset = holeList[i-1];
                }
            }
        }
    }
    if (bestSpace == 65537) {
        return -1;
    }
    else {
        return bestHoleOffset;
    }
};

int worstFit(int sizeInWords, void *list) {
    uint16_t* holeList = (uint16_t*) list;
    int size = (holeList[0] * 2) + 1;
    int worstSpace = -1;
    int worstHoleOffset;
    for (unsigned int i = 1; i < size; i++) {
        if ((i % 2) == 0) {
            int holeLength = holeList[i];
            if (sizeInWords <= holeLength) {
                int space = holeLength - sizeInWords;
                if (space > worstSpace) {
                    worstSpace = space;
                    worstHoleOffset = holeList[i - 1];
                }
            }
        }
    }
    if (worstSpace == -1) {
        return -1;
    }
    else {
        return worstHoleOffset;
    }
};