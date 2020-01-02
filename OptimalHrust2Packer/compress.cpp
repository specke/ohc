
/*
Copyright (c) 2015-2020 Eugene Larchenko, el6345@gmail.com
Published under the MIT License
*/


#include "compress.h"
#include <Windows.h>

// "hr21" + word + word
#define HEADER_SIZE 8

Compressor::Compressor()
{
	memset(this, 0, sizeof(*this));
};

// Try compress and fallback to Store method if necessary
void Compressor::CompressAuto()
{
	if (InputSize < 6 + 1)
	{
		// compression impossible, use Store method
		CompressStore();
	}
	else
	{
		int storedSize = GetStoredPackedSize();
		Compress_Preprocess();
		if (
			storedSize <= compressedSize || // файл не жмётся
			compressedSize > 0xFFFF			// невозможно сформировать заголовок
			)
		{
			CompressStore();
			Stored = true;
		}
		else
		{
			Compress_Emit();
			Stored = false;
		}

		ProgressReport.Done();
	}
}

int Compressor::GetStoredPackedSize()
{
	return InputSize + HEADER_SIZE;
};

void Compressor::CompressStore()
{
	Output[0] = 'h';
	Output[1] = 'r';
	Output[2] = '2';
	Output[3] = '1' + 0x80;
	Output[4] = byte(InputSize >> 0);
	Output[5] = byte(InputSize >> 8);
	Output[6] = byte(InputSize >> 0);
	Output[7] = byte(InputSize >> 8);
	memmove(&Output[8], Input, InputSize);
	
	OutputSize = InputSize + HEADER_SIZE;
};

void Compressor::Compress_Preprocess()
{
	if (InputSize < 6 + 1)
	{
		// применить сжатие невозможно
		throw;
	}

	optimalCompressor.ProgressReport = &this->ProgressReport;
	optimalCompressor.Init(Input, InputSize - 6); // last 6 bytes are never compressed
	int packedBitsCount = optimalCompressor.Preprocess();
	
	packedBitsCount += 6 + 8; // end of stream literal

	compressedSize =
		HEADER_SIZE +
		6 + // last 6 bytes
		(packedBitsCount + 7) / 8;
};

void Compressor::emitByte(int byte_) 
{
	*outputPtr++ = (byte)byte_;
};

void Compressor::emitBit(int bit) 
{
	if (controlBitsCnt == 8)
	{
		controlBitsCnt = 0;
	}

	if (controlBitsCnt == 0)
	{
		controlBytePtr = outputPtr;
		*outputPtr++ = 0;
	}
	
	*controlBytePtr = (*controlBytePtr) * 2 + (bit & 1);
	controlBitsCnt++;
};

void Compressor::finalizeBitFlow()
{
	if (controlBitsCnt > 0)
	{
		while(controlBitsCnt < 8)
			emitBit(0);
	}
};

void Compressor::emitLargeCnt(int cnt)
{
    if (cnt < 3)
	{
		throw; // something is wrong
	}
    if (cnt == 3)
    {
        emitBit(1);
        emitBit(0);
    }
    else if (cnt < 16) // 4..15
    {
        for (int i = 0; i < 5 && cnt >= 0; i++)
        {
            int t = min(cnt, 3);
            emitBit(t >> 1);
            emitBit(t >> 0);
            cnt -= 3;
        };
    }
    else // 16..0xFFF
    {
        emitBit(1);
        emitBit(1);
        emitBit(0);
        emitBit(0);
        emitBit(1);
        if (cnt < 256)
        {
            emitByte(cnt);
        }
        else
        {
            if (cnt > 0xFFF) throw;
            emitByte(cnt >> 8);
            emitByte(cnt >> 0);
        }
    }
};

void Compressor::emitLongDist(int dist)
{
    int H = dist >> 8;
    if (H == -1)
    {
        emitBit(1);
    }
    else
    {
        emitBit(0);
        if (H >= -3)
        {
            emitBit(1);
            emitBit(1);
            emitBit(~H);
        }
        else if (H >= -7)
        {
            emitBit(1);
            emitBit(0);
            H += 3;
            emitBit(H >> 1);
            emitBit(H >> 0);
        }
        else if (H >= -15)
        {
            emitBit(0);
            emitBit(1);
            H += 7;
            emitBit(H >> 2);
            emitBit(H >> 1);
            emitBit(H >> 0);
        }
        else // H <= -16
        {
            emitBit(0);
            emitBit(0);
            if (H >= -30)
            {
                H += 15;
                emitBit(H >> 3);
                emitBit(H >> 2);
                emitBit(H >> 1);
                emitBit(H >> 0);
            }
            else
            {
                if (dist < -65535) throw;
                emitBit(0);
                emitBit(0);
                emitBit(0);
                emitBit(0);
                emitByte(H);
            }
        }
    }

    emitByte(dist & 0xFF);
};

// Builds final compressed block using precalculations 
// made by Compress_Preprocess routine
void Compressor::Compress_Emit() {

	if (compressedSize > 0xFFFF)
	{
		// невозможно сформировать заголовок
		throw;
	}

	outputPtr = Output;
	controlBitsCnt = 0;

	// Header

	int packedSize = compressedSize - HEADER_SIZE;
	emitByte('h');
	emitByte('r');
	emitByte('2');
	emitByte('1');
	emitByte(InputSize >> 0);
	emitByte(InputSize >> 8);
	emitByte(packedSize >> 0);
	emitByte(packedSize >> 8);

    // backup last 6 bytes

	for (int i = 0; i < 6; i++)
		emitByte(Input[InputSize - 6 + i]);

	// Compressed data

	int endpos = InputSize - 6; // omit last 6 bytes
    int pos = 0;

	emitByte(Input[pos++]);	// first byte is simply copied

    while (pos != endpos)
    {
        if (pos > endpos) throw; // something is wrong
	
		Backref cmd = optimalCompressor.GetOptimalOp(pos);

        if (cmd.Count == 0)
        {
            throw;
        }
        else if (cmd.Count == -1) // copy 1 byte
        {
            emitBit(1);
            emitByte(Input[pos++]);
        }
        else if (cmd.Count < -1) // copy 12..42 bytes
        {
            int cnt = -cmd.Count;
            if (cnt < 12 || cnt > 42 || cnt % 2 != 0) throw;
            emitBit(0);
            emitBit(1);
            emitBit(1);
            emitBit(0);
            emitBit(0);
            emitBit(0);
            int c = (cnt - 12) / 2;
            for (int i = 3; i >= 0; i--) emitBit(c >> i);
			for (int i = 0; i < cnt; i++) emitByte(Input[pos++]);
        }
        else // backreference
        {
            if (cmd.Dist >= 0)
			{
				throw;
			}
            if (cmd.Count == 1)
            {
                if (cmd.Dist < -8) throw;
                emitBit(0);
                emitBit(0);
                emitBit(0);
                emitBit(cmd.Dist >> 2);
                emitBit(cmd.Dist >> 1);
                emitBit(cmd.Dist >> 0);
            }
            else if (cmd.Count == 2)
            {
                if (cmd.Dist < -256) throw;
                emitBit(0);
                emitBit(0);
                emitBit(1);
                emitByte((byte)cmd.Dist);
            }
            else
            {
                emitBit(0);
                emitLargeCnt(cmd.Count);
                emitLongDist(cmd.Dist);
            }
            pos += cmd.Count;
        }
	}

    // end of stream marker

    emitBit(0);
    emitBit(1);
    emitBit(1);
    emitBit(0);
    emitBit(0);
    emitBit(1);
    emitByte(0);

	// finally

	finalizeBitFlow();

	size_t resultCompressedSize = outputPtr - Output;
	if (resultCompressedSize != compressedSize)
		throw; // something wrong

	if (compressedSize > ARRAYSIZE(Output))
		throw; // buffer overflow; could not happen

	OutputSize = compressedSize;
}


////////////////////////////////////////////////////////////
///////////       OptimalCompressor         ////////////////
////////////////////////////////////////////////////////////


void OptimalCompressor::Init(byte* input, int inputSize)
{
	this->inputSize = inputSize;
	memmove(this->input, input, inputSize);
}

Backref OptimalCompressor::GetOptimalOp(int pos)
{
	if (pos < 1) throw;
	if (pos >= inputSize) throw;
	//if (cost[pos] < 0) throw;
	return solution[pos];
};

int OptimalCompressor::Preprocess()
{
	memmove(input + inputSize, input, inputSize);
	inputOffset = inputSize;

	// solve optimization problem using Dynamic Programming.
	// DP base param is the position in input file.

	cost[inputSize] = 0;

    for (int pos = inputSize - 1; pos >= 1; pos--)
    {
		if ((pos & 0x3FF) == 0)
		{
			if (ProgressReport)
				ProgressReport->Report(inputSize, inputSize - pos);
		}

        int result;

        // try copy 1 byte

        result = 1 + 8 + cost[pos + 1];
		Backref resultOp(-1, 0);

        // try copy 12, 14..42 bytes

        for (int i = 0; i < 16; i++)
        {
            int cnt = i * 2 + 12;
            if (pos + cnt > inputSize) {
				break;
			}
            int t = 6 + 4 + cnt * 8 + cost[pos + cnt];
            if (t < result) {
				result = t; resultOp = Backref(-cnt, 0); 
			}
        }

        // try backreferences

        {
			fill_matchLen(pos);
            int cnt = 0;
            int nextPos = pos;
            for (int dist = -1; dist >= -pos; dist--)
            {
				//if (dist < -0xFFFF) break;
                int matchCnt = matchLen[dist + inputSize];

                while (cnt + 1 <= matchCnt)
                {
                    if (nextPos >= inputSize) {
						goto break_dist_loop;
					}
                    if (cnt >= 0xFFF) { // backref cnt limit
						goto break_dist_loop;
					}
                    cnt++;
                    nextPos++;

                    Backref br(cnt, dist);
                    int t = br.GetEncodedLen() + cost[pos + cnt];
                    if (t < result) {
						result = t; resultOp = br; 
					}
                }
            }
			break_dist_loop: ;
        }

        cost[pos] = result;
        solution[pos] = resultOp;
    }

	// return compressed size in bits

	return 
		8 + // first byte simply copied
		cost[1];
};

// Finds longest match for every possible reference distance
void OptimalCompressor::fill_matchLen(int pos)
{
    if (pos >= inputSize) throw;

    int sstart = inputOffset - (inputSize - pos);
    int n = inputSize + (inputSize - pos);

	#define z matchLen
	byte* s = input + sstart;

	// © https://e-maxx.ru/algo/z_function

    int l = 0, r = 0;
    for (int i = 1; i < inputSize; i++)
    {
        int zi;
        if (i > r)
            zi = 0;
        else
            zi = min(z[i - l], r + 1 - i);

        while (i + zi < n && s[i + zi] == s[zi])
        {
            zi++;
        }

        if (i + zi - 1 > r)
        {
            r = i + zi - 1;
            l = i;
        }

        z[i] = zi;
    }

    // z[0] is undefined
}

static const int encodedCntLen[16] = { -1, -1, -1, 3, 5, 5, 7, 7, 7, 9, 9, 9, 11, 11, 11, 11 };
static const byte encodedDistLen[] = {
	23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,	// H = -30..-16
	14,14,14,14,14,14,14,14,						// H = -15..-8
	13,13,13,13,									// H = -7..-4
	12,12,											// H = -3..-2
	9												// H = -1
};

int Backref::GetEncodedLen()
{
    //if (Count <= 0) throw;
    //if (Dist >= 0) throw;

	#define infinity 0x0FFFFFFF
	#define impossible infinity

    if (Count == 1) return (Dist >= -8) ? 6 : impossible;
    if (Count == 2) return (Dist >= -256) ? 3 + 8 : impossible;

	//if (Dist < -0xFFFF) throw;
	int distBits = encodedDistLen[(Dist >> 8) + 256];  // 9...23

	if (Count < 16) return encodedCntLen[Count] + distBits;
    if (Count < 256) return (6 + 8) + distBits;
    if (Count < 0x1000) return (6 + 8 + 8) + distBits;
    return impossible;

	#undef impossible
};

