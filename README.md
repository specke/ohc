# Optimal Hrust Compressor

*Hrust* is a data compressor on ZX Spectrum platform. Here is compressor implementation for PC in C language.

The major thing about this implementation is that it achieves maximum compression possible for *Hrust* compression scheme. 

There are two compressors: for *Hrust 1.3* and *Hrust 2.1* formats.

### About compression algorithm

To find the smallest compressed sequence of all possible, we solve optimization problem using Dynamic Programming.

For finding sequence matches, we use [Z algorithm](https://codeforces.com/blog/entry/3107).

The resulting algorithm complexity is *O*(*n*<sup>2</sup>).
