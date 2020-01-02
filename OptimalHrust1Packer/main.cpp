
#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include "compress.h"
#include <time.h>

Compressor compressor;

void PrintVersion()
{
	printf("\n");
	printf("Optimal Hrust 1.3 compressor, ");
	#ifdef _WIN64
		printf("x64\n");
	#else
		printf("x86\n");
	#endif
	printf("version 2015.03.10\n");
	printf("by Eugene Larchenko\n");
	printf("\n");
}

void PrintUsage()
{
	printf("Usage:\n");
	printf("oh1c.exe <input> [<output>]\n");
	printf("\n");
}

int main(int argc, const char* argv[])
{
	PrintVersion();

	if (argc < 2 || argc > 3)
	{
		PrintUsage();
		return 1;
	}

	const char* inputPath = argv[1];
	
	char outputPath[1000];
	const char* s = (argc >= 3) ? argv[2] : argv[1];
	size_t sl = strlen(s);
	if (sl + 10 > ARRAYSIZE(outputPath))
	{
		printf("Path is too long\n");
		return 2;
	}
	strcpy(outputPath, s);
	if (argc < 3)
	{
		strcat(outputPath, ".HR");
	}


	int result = 0;

	FILE* fIn = fopen(inputPath, "r+b");
	if (!fIn)
	{
		printf("Error opening input file\n");
		result = 5;
	}
	else
	{
		size_t fsize = fread(compressor.Input, 1, MAX_INPUT_SIZE + 1, fIn);
		fclose(fIn);
		if (fsize > MAX_INPUT_SIZE)
		{
			printf("Input file is too large. Max supported file size is %d bytes.\n", MAX_INPUT_SIZE);
			result = 4;
		}
		else
		{
			printf("Compressing file: %s\n", inputPath);

			compressor.InputSize = (int)fsize;

			clock_t t0 = clock();
			compressor.TryCompress();
			clock_t t1 = clock();
			
			if (compressor.Result == COMPRESS_RESULT::IMPOSSIBLE_TOO_SMALL)
			{
				printf("ERROR!\nCannot compress files smaller than 7 bytes.\n", outputPath);
				result = 4;
			}
			else
			{
				double duration = (double)(t1 - t0) / CLOCKS_PER_SEC;
				printf("time = %.3f \n", duration);

				double ratio = (double)compressor.OutputSize / compressor.InputSize;
				//if (ratio > 1) ratio = max(ratio, 1.001);
				char* ratioWarning = (compressor.OutputSize >= compressor.InputSize) ? "(!)" : "";
				printf("compression: %d / %d = %.3f%s\n", compressor.OutputSize, compressor.InputSize, ratio, ratioWarning);

				if (compressor.Result == COMPRESS_RESULT::IMPOSSIBLE_TOO_BAD)
				{
					printf("ERROR!\nCannot save compressed file because it is larger than 65535 bytes.\n", outputPath);
					result = 4;
				}
				else
				{
					printf("Writing compressed file: %s\n", outputPath);
					FILE* fOut = fopen(outputPath, "wb");
					if (!fOut)
					{
						printf("Error writing output file\n");
						result = 5;
					}
					else
					{
						size_t written = fwrite(compressor.Output, 1, compressor.OutputSize, fOut);
						fclose(fOut);
						if (written != compressor.OutputSize)
						{
							// delete incomplete compressed file
							remove(argv[2]);
							printf("Error writing output file\n");
							result = 5;
						}
						else
						{
							printf("All OK\n");
							result = 0;
						}
					}
				}
			}
		}
	}

	printf("\n");
	return result;
};

