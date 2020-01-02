
#pragma once

#include <Windows.h>
#include "progressReport.h"

const int MAX_INPUT_SIZE = 0xFFFF;

enum COMPRESS_RESULT
{
	OK,
	IMPOSSIBLE_TOO_SMALL,  // can't compress files smaller than 7 bytes
	IMPOSSIBLE_TOO_BAD     // compressed size is above 0xFFFF, can't make header
};

// sizeof(Backref) = 8
struct Backref
{
	int Dist;
	short Count; // negative value means not reference, but inserting immediate bytes
	bool IsRIR;  // is compound reference (ref + insert + ref)
	byte D;      // 1..8 (1 is senseless though)

	Backref() {};

	Backref(bool isRIR, int count, int dist, int D) 
		: IsRIR(isRIR), Dist(dist), Count(short(count)), D(byte(D)) {};

	int GetEncodedLen();
};

class OptimalCompressor
{
private:

	int inputSize;
	int inputOffset;
	byte input[MAX_INPUT_SIZE * 2];

	int cost[MAX_INPUT_SIZE + 1][8];
	Backref solution[MAX_INPUT_SIZE + 1][8];

	int matchLen[MAX_INPUT_SIZE];
	void fill_matchLen(int pos);

public:

	ProgressReport* ProgressReport;

	OptimalCompressor() {};
	void Init(byte* input, int inputSize);
	int Preprocess(); // returns compressed size in bits
	Backref GetOptimalOp(int pos, int dd);
};

class Compressor
{
private:

	OptimalCompressor optimalCompressor;

	byte* outputPtr;
	void emitByte(int byte);

	WORD* controlWordPtr;
	int controlBitsCnt;
	void emitBit(int bit);
	void finalizeBitFlow();

	void emitLargeCnt(int cnt);
	void emitLongDist(int dist, int dd);

public:

	int InputSize;
	byte Input[MAX_INPUT_SIZE + 1];

	#define maxOutputSize (int(MAX_INPUT_SIZE * 1.05 + 100))
	byte Output[maxOutputSize];
	int OutputSize;
	COMPRESS_RESULT Result;

	Compressor();
	void Compressor::TryCompress();

	ProgressReport ProgressReport;

private:

	// approximate (may be 1 byte less) compressed size in bytes. Set by Compress_Preprocess().
	int compressedSizePrecalc;

	// Performs actual compression but doesn't output compressed data yet
	void Compress_Preprocess();

	// Builds final compressed block
	void Compress_Emit();

};

