/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rsLocklessFifo.h"

using namespace android;

#include <utils/Log.h>

LocklessCommandFifo::LocklessCommandFifo()
{
}

LocklessCommandFifo::~LocklessCommandFifo()
{
}

bool LocklessCommandFifo::init(uint32_t sizeInBytes)
{
    // Add room for a buffer reset command
    mBuffer = static_cast<uint8_t *>(malloc(sizeInBytes + 4));
    if (!mBuffer) {
        LOGE("LocklessFifo allocation failure");
        return false;
    }

    int status = pthread_mutex_init(&mMutex, NULL);
    if (status) {
        LOGE("LocklessFifo mutex init failure");
        free(mBuffer);
        return false;
    }
    status = pthread_cond_init(&mCondition, NULL);
    if (status) {
        LOGE("LocklessFifo condition init failure");
        pthread_mutex_destroy(&mMutex);
        free(mBuffer);
        return false;
    }

    mSize = sizeInBytes;
    mPut = mBuffer;
    mGet = mBuffer;
    mEnd = mBuffer + (sizeInBytes) - 1;
    dumpState("init");
    return true;
}

uint32_t LocklessCommandFifo::getFreeSpace() const 
{
    int32_t freeSpace = 0;
    //dumpState("getFreeSpace");

    if (mPut >= mGet) {
        freeSpace = mEnd - mPut;
    } else {
        freeSpace = mGet - mPut;
    }

    if (freeSpace < 0) {
        freeSpace = 0;
    }
    
    //LOGE("free %i", freeSpace);
    return freeSpace;
}

bool LocklessCommandFifo::isEmpty() const
{
    return mPut == mGet;
}


void * LocklessCommandFifo::reserve(uint32_t sizeInBytes)
{
    // Add space for command header and loop token;
    sizeInBytes += 8;

    //dumpState("reserve");
    if (getFreeSpace() < sizeInBytes) {
        makeSpace(sizeInBytes);
    }

    return mPut + 4;
}

void LocklessCommandFifo::commit(uint32_t command, uint32_t sizeInBytes)
{
    //LOGE("commit cmd %i  size %i", command, sizeInBytes);
    //dumpState("commit 1");
    reinterpret_cast<uint16_t *>(mPut)[0] = command;
    reinterpret_cast<uint16_t *>(mPut)[1] = sizeInBytes;
    mPut += ((sizeInBytes + 3) & ~3) + 4;
    //dumpState("commit 2");

}

void LocklessCommandFifo::commitSync(uint32_t command, uint32_t sizeInBytes)
{
    commit(command, sizeInBytes);
    flush();
}

void LocklessCommandFifo::flush()
{
    //dumpState("flush 1");
    while(mPut != mGet) {
        usleep(1);
    }
    //dumpState("flush 2");
}

const void * LocklessCommandFifo::get(uint32_t *command, uint32_t *bytesData)
{
    while(1) {
        while(isEmpty()) {
            usleep(10);
        }
        //dumpState("get 3");

        *command = reinterpret_cast<const uint16_t *>(mGet)[0];
        *bytesData = reinterpret_cast<const uint16_t *>(mGet)[1];
        //LOGE("Got %i, %i", *command, *bytesData);
    
        if (*command) {
            // non-zero command is valid
            return mGet+4;
        }
    
        // zero command means reset to beginning.
        mGet = mBuffer;
    }
}

void LocklessCommandFifo::next()
{
    uint32_t bytes = reinterpret_cast<const uint16_t *>(mGet)[1];
    mGet += ((bytes + 3) & ~3) + 4;
    //dumpState("next");
}

void LocklessCommandFifo::makeSpace(uint32_t bytes)
{
    //dumpState("make space");
    if ((mPut+bytes) > mEnd) {
        // Need to loop regardless of where get is.
        while((mGet > mPut) && (mBuffer+4 >= mGet)) {
            sleep(1);
        }

        // Toss in a reset then the normal wait for space will do the rest.
        reinterpret_cast<uint16_t *>(mPut)[0] = 0;
        reinterpret_cast<uint16_t *>(mPut)[1] = 0;
        mPut = mBuffer;
    }

    // it will fit here so we just need to wait for space.
    while(getFreeSpace() < bytes) {
        sleep(1);
    }
    
}

void LocklessCommandFifo::dumpState(const char *s) const
{
    LOGE("%s  put %p, get %p,  buf %p,  end %p", s, mPut, mGet, mBuffer, mEnd);
}

