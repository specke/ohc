
#pragma once

#include <Windows.h>
#include "progressReport.h"

const int MAX_INPUT_SIZE = 0xFFFF;

struct Backref
{
	int Dist;
	int Count;

	Backref() {};

	Backref(int count, int dist) 
		: Dist(dist), Count(count) {};

	int GetEncodedLen();
};

class OptimalCompressor
{
private:

	int inputSize;
	int inputOffset;
	byte input[MAX_INPUT_SIZE * 2];

	int cost[MAX_INPUT_SIZE + 1];
	Backref solution[MAX_INPUT_SIZE + 1];

	int matchLen[MAX_INPUT_SIZE];
	void fill_matchLen(int pos);

public:

	ProgressReport* ProgressReport;

	OptimalCompressor() {};
	void Init(byte* input, int inputSize);
	int Preprocess();	// returns compressed size in bits
	Backref GetOptimalOp(int pos);
};

class Compressor
{
private:

	OptimalCompressor optimalCompressor;

	byte* outputPtr;
	void emitByte(int byte);

	byte* controlBytePtr;
	int controlBitsCnt;
	void emitBit(int bit);
	void finalizeBitFlow();

	void emitLargeCnt(int cnt);
	void emitLongDist(int dist);

public:

	int InputSize;
	byte Input[MAX_INPUT_SIZE + 1];

	#define maxOutputSize (int(MAX_INPUT_SIZE * 1.05 + 100))
	byte Output[maxOutputSize];
	int OutputSize;
	bool Stored; // Store method used?

	Compressor();

	// Do compressing. Fallback to Store method if necessary.
	void Compressor::CompressAuto();

	ProgressReport ProgressReport;

private:

	int GetStoredPackedSize();
	void CompressStore();

	// Compressed size in bytes (including header size). Set by Compress_Preprocess().
	int compressedSize;

	// Performs actual compression but doesn't output compressed data yet
	void Compress_Preprocess();

	// Builds final compressed block
	void Compress_Emit();

};

