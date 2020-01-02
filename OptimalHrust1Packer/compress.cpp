
#include "compress.h"
#include <Windows.h>

// D register defines current compression window size.
// Expanding it takes special 13-bit literal.
#define CHANGE_D_LEN (5 + 8)

Compressor::Compressor()
{
	memset(this, 0, sizeof(*this));
};


void Compressor::TryCompress()
{
	if (InputSize < 6 + 1)
	{
		// compression impossible
		Result = COMPRESS_RESULT::IMPOSSIBLE_TOO_SMALL;
	}
	else
	{
		Compress_Preprocess();
		Compress_Emit();
		if (OutputSize > 0xFFFF)
		{
			// невозможно сформировать заголовок
			Result = COMPRESS_RESULT::IMPOSSIBLE_TOO_BAD;
		}
		else
		{
			*(WORD*)&Output[4] = (WORD)OutputSize; // set packed size in header
			Result = COMPRESS_RESULT::OK;
		}

		ProgressReport.Done();
	}
}

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

	packedBitsCount += 7 + 7; // end of stream literal

	compressedSizePrecalc =
		6 + // header
		6 + // last 6 bytes
		(packedBitsCount + 7) / 8; 
	// NOTE: calculated result may be 1 byte less than it should because of unused bits in last bitflow word
};

void Compressor::emitByte(int byte_) 
{
	*outputPtr++ = (byte)byte_;
};

void Compressor::emitBit(int bit) 
{
	if (controlBitsCnt >= 16) throw; // should never happen

	*controlWordPtr = (*controlWordPtr) * 2 + (bit & 1);
	controlBitsCnt++;

	if (controlBitsCnt == 16)
	{
		controlBitsCnt = 0;
		controlWordPtr = (WORD*)outputPtr;
		*outputPtr++ = 0;
		*outputPtr++ = 0;
	}
};

void Compressor::finalizeBitFlow()
{
	if (controlBitsCnt == 0)
	{
		// remove last control word if it is empty
		outputPtr -= 2;
		if ((byte*)controlWordPtr != outputPtr) throw;
	}
	else
	{
		for( ; controlBitsCnt != 16; controlBitsCnt++)
			*controlWordPtr <<= 1;
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
    else // 16..0xEFF
    {
        emitBit(1);
        emitBit(1);
        emitBit(0);
        emitBit(0);
        emitBit(0);
        emitBit(0);
        if (cnt < 128)
        {
            for (int i = 6; i >= 0; i--) emitBit(cnt >> i);
        }
        else
        {
            if (cnt > 0xEFF) throw;
            for (int i = 6; i >= 0; i--) emitBit(cnt >> 8 >> i);
			emitByte(cnt);
        }
    }
};

void Compressor::emitLongDist(int dist, int D)
{
	if (dist >= 0)
	{
		throw; // should never happen
	}
    if (dist >= -32)
    {
        emitBit(1);
        emitBit(0);
        for (int i = 4; i >= 0; i--) emitBit(dist >> i);
    }
    else if (dist >= -256)
    {
        emitBit(0);
        emitBit(1);
        emitByte((byte)dist);
    }
    else if (dist >= -512)
    {
        emitBit(0);
        emitBit(0);
        emitByte((byte)dist);
    }
    else
    {
        if (dist < -65535) throw;  // some redundant checks
        int H = dist >> 8;
		if (D < 2 || D > 8) throw;
        if (H < -(1 << D)) throw;
        emitBit(1);
        emitBit(1);
        for (int i = D - 1; i >= 0; i--) emitBit(H >> i);
        emitByte((byte)dist);
    }
};

// Builds final compressed block using precalculations 
// made by Compress_Preprocess routine
void Compressor::Compress_Emit() {

	outputPtr = Output;
	
	// Header

	emitByte('H');
	emitByte('R');
	emitByte(InputSize >> 0);
	emitByte(InputSize >> 8);
	emitByte(0); // placeholder for
	emitByte(0); // compressed size

    // backup last 6 bytes

	for (int i = 0; i < 6; i++)
		emitByte(Input[InputSize - 6 + i]);

	// emit first bitflow word

	controlBitsCnt = 0;
	controlWordPtr = (WORD*)outputPtr;
	*outputPtr++ = 0;
	*outputPtr++ = 0;

	// Compressed data

	int endpos = InputSize - 6; // omit last 6 bytes
    int pos = 0;

	emitByte(Input[pos++]);	// first byte is simply copied

    // value of D register that controls maximum reference distance
	int D = 2;

    while (pos != endpos)
    {
        if (pos > endpos) throw; // something is wrong
	
		Backref cmd = optimalCompressor.GetOptimalOp(pos, D);

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
            emitBit(1);
            int c = (cnt - 12) / 2;
            for (int i = 3; i >= 0; i--) emitBit(c >> i);
			for (int i = 0; i < cnt; i++) emitByte(Input[pos++]);
        }
        else // RIR or backref
        {
            if (cmd.IsRIR)
            {
                emitBit(0);
                if (cmd.Dist >= -16)
                {
                    emitBit(1);
                    emitBit(1);
                    emitBit(0);
                    emitBit(0);
                    emitBit(1);
                    for (int i = 3; i >= 0; i--) emitBit(cmd.Dist >> i);
                }
                else if ((cmd.Dist & 1) == 0)
                {
                    emitBit(0);
                    emitBit(1);
                    emitBit(1);
                    emitBit(0);
                    int t = (((cmd.Dist + 16 - 1) ^ 2) - 1) >> 1;
                    emitByte((byte)t);
                }
                else
                {
                    emitBit(1);
                    emitBit(0);
                    emitBit(0);
                    emitBit(1);
                    int t = (((cmd.Dist + 16 - 1) ^ 3) - 1) >> 1;
                    emitByte((byte)t);
                }
				emitByte(Input[pos + 1]);
                pos += 3;
            }
			else // backref
			{
				if (cmd.Dist >= 0)
					throw;

                if (cmd.Count >= 3)
                {
                    // change_D(cmd.D);
					{
						if (cmd.D < 1 || cmd.D > 8) throw;
						while (D != cmd.D)
						{
							D = (D & 7) + 1;
							emitBit(0);
							emitBit(0);
							emitBit(1);
							emitBit(1);
							emitBit(0);
							emitByte(0xFE);
						}
					};
                    
					emitBit(0);
                    emitLargeCnt(cmd.Count);
                    emitLongDist(cmd.Dist, D);
                }

                else if (cmd.Count == 2)
                {
                    emitBit(0);
                    emitBit(0);
                    emitBit(1);
                    if (cmd.Dist >= -32)
                    {
                        emitBit(1);
                        emitBit(1);
                        for (int i = 4; i >= 0; i--) emitBit(cmd.Dist >> i);
                    }
                    else if (cmd.Dist >= -256)
                    {
                        emitBit(1);
                        emitBit(0);
						emitByte((byte)cmd.Dist);
                    }
                    else if (cmd.Dist >= -512)
                    {
                        emitBit(0);
                        emitBit(1);
                        emitByte((byte)cmd.Dist);
                    }
                    else if (cmd.Dist >= -768)
                    {
                        emitBit(0);
                        emitBit(0);
                        emitByte((byte)cmd.Dist);
                    }
                    else
                    {
                        throw;
                    }
                }

                else if (cmd.Count == 1)
                {
                    if (cmd.Dist < -8) throw;
                    emitBit(0);
                    emitBit(0);
                    emitBit(0);
                    for (int i = 2; i >= 0; i--) emitBit(cmd.Dist >> i);
                }

                else
                {
                    throw;
                }

				pos += cmd.Count;
			}
		}
	}

    // end of stream marker

    emitBit(0);
    emitBit(1);
    emitBit(1);
    emitBit(0);
    emitBit(0);
    emitBit(0);
    emitBit(0);
    for (int i = 6; i >= 0; i--) emitBit(15 >> i);

	// finally

	finalizeBitFlow();

	int resultCompressedSize = int(outputPtr - Output);
	if (resultCompressedSize < compressedSizePrecalc || resultCompressedSize > compressedSizePrecalc + 1)
		throw; // something is wrong

	if (resultCompressedSize > ARRAYSIZE(Output))
		throw; // buffer overflow; could not happen

	OutputSize = resultCompressedSize;
}


////////////////////////////////////////////////////////////
///////////       OptimalCompressor         ////////////////
////////////////////////////////////////////////////////////


void OptimalCompressor::Init(byte* input, int inputSize)
{
	this->inputSize = inputSize;
	memmove(this->input, input, inputSize);
}

Backref OptimalCompressor::GetOptimalOp(int pos, int D)
{
	if (pos < 1) throw;
	if (pos >= inputSize) throw;
	if (D < 2 || D > 8) throw;
	return solution[pos][D - 1];
};

int OptimalCompressor::Preprocess()
{
	memmove(input + inputSize, input, inputSize);
	inputOffset = inputSize;

	// solve optimization problem using Dynamic Programming.
	// DP base params are position in input file and the value of D register.

	for(int D = 1; D <= 8; D++)
		cost[inputSize][D - 1] = 0;
	
    for (int pos = inputSize - 1; pos >= 1; pos--)
    {
		if ((pos & 0x1FF) == 0)
		{
			if (ProgressReport)
				ProgressReport->Report(inputSize, inputSize - pos);
		}

        int* result = cost[pos];
		Backref* resultOp = solution[pos];

		// try all possible values of D register
        for (byte D = 2 - 1; D <= 8 - 1; D++)
        {
            // try copy 1 byte

            result[D] = 1 + 8 + cost[pos + 1][D];
            resultOp[D] = Backref(false, -1, 0, D+1);

            // try copy 12, 14..42 bytes

            for (int i = 0; i < 16; i++)
            {
                int cnt = i * 2 + 12;
                if (pos + cnt > inputSize) {
					break;
				}
                int t = 7 + 4 + cnt * 8 + cost[pos + cnt][D];
				if (t < result[D]) { 
					result[D] = t; resultOp[D] = Backref(false, -cnt, 0, D+1); 
				}
            }

            // try RIR

            for (int copyPos = pos - 1; copyPos >= 0; copyPos--)
            {
                int dist = copyPos - pos;
                if (dist < -79) {
					break;
				}
                int hl = copyPos;
                int de = pos;
                if (de + 3 > inputSize) {
					break;
				}
                if (input[hl] == input[de] && input[hl + 2] == input[de + 2])
                {
                    Backref br(true, 3, dist, 0);
                    int t = br.GetEncodedLen() + cost[pos + 3][D];
                    if (t < result[D]) { 
						result[D] = t; resultOp[D] = br; 
					}
                    break;
                }
            }
        }

        // try backreferences

        {
			fill_matchLen(pos);
            int cnt = 0;
            int nextPos = pos;
            for (int dist = -1; dist >= -pos; dist--)
            {
                int matchCnt = matchLen[dist + inputSize];

                while (cnt + 1 <= matchCnt)
                {
                    if (nextPos >= inputSize) {
						goto break_dist_loop;
					}
                    if (cnt >= 0xEFF) { // backref cnt limit
						goto break_dist_loop;
					}
                    cnt++;
                    nextPos++;

                    for (int new_D = 2 - 1; new_D <= 8 - 1; new_D++)
                    {
                        Backref br(false, cnt, dist, new_D + 1);
                        int t2 = br.GetEncodedLen() + cost[nextPos][new_D];
                        //for (int D = 2 - 1; D <= new_D; D++) // this loop version disables D cycling
                        for (int D = 2 - 1; D <= 8 - 1; D++)
                        {
							int D_change_cost = ((new_D - D) & 7) * CHANGE_D_LEN;
                            int t = D_change_cost + t2;
                            if (t < result[D]) { 
								result[D] = t; resultOp[D] = br; 
							}
                        }
					}
				}
            }
			break_dist_loop: ;
        }
    }

	// return compressed size in bits

	int start_D = 2;
	return
		8 + // first byte simply copied
		cost[1][start_D - 1];
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

int Backref::GetEncodedLen()
{
    //if (Count <= 0) throw;
    //if (Dist >= 0) throw;

	#define infinity 0x0FFFFFFF
	#define impossible infinity

    if (IsRIR)
    {
        if (Dist >= -16) return 6 + 4 + 8;
        if (Dist >= -79) return 5 + 8 + 8; // alternative: 3+2+8+8
        throw; // should never happen
    }
    else
    {
        if (Count == 1) return (Dist >= -8) ? 6 : impossible;

        if (Count == 2)
        {
            if (Dist >= -32) return (5 + 5);
            //if (Dist >= -256) return (5 + 8);
            if (Dist >= -768) return (5 + 8);
            return impossible;
        }

        //if (Count < 3 || Count > 0xEFF) throw;
		static const int encodedCntLen[16] = { -1, -1, -1, 3, 5, 5, 7, 7, 7, 9, 9, 9, 11, 11, 11, 11 };
		int cntBits =
            Count < 16 ? encodedCntLen[Count] :
            Count < 128 ? 7 + 7 :
            7 + 7 + 8;

        int distBits;
        if (Dist >= -32) distBits = 2 + 5;
        //else if (Dist >= -256) distBits = 2 + 8;
        else if (Dist >= -512) distBits = 2 + 8;
        else {
            //if (Dist < -0xFFFF) throw;
            int H = Dist >> 8;
            //if (D < 1 || D > 8) throw;
            if (H < -(1 << D))
                return impossible;
            else
				distBits = 2 + D + 8;
        }

        return cntBits + distBits;
    }

	#undef impossible
};

