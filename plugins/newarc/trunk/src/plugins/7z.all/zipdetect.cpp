#include "7z.h"

struct ZipHeader
{
  DWORD Signature;
  WORD VerToExtract;
  WORD BitFlag;
  WORD Method;
  WORD LastModTime;
  WORD LastModDate;
  DWORD Crc32;
  DWORD SizeCompr;
  DWORD SizeUncompr;
  WORD FileNameLen;
  WORD ExtraFieldLen;
  // FileName[];
  // ExtraField[];
};

const size_t MIN_HEADER_LEN=sizeof(ZipHeader);

inline BOOL IsValidHeader(const unsigned char *Data, const unsigned char *DataEnd)
{
  ZipHeader* pHdr=(ZipHeader*)Data;
  //const WORD Zip64=45;
  return (0x04034b50==pHdr->Signature
    && pHdr->Method<15
    && pHdr->VerToExtract < 0xFF
    && Data+MIN_HEADER_LEN+pHdr->FileNameLen+pHdr->ExtraFieldLen<DataEnd);
}

int IsZipHeader(const unsigned char *Data,int DataSize)
{
	if (DataSize>=4 && Data[0]=='P' && Data[1]=='K' && Data[2]==5 && Data[3]==6)
	{
		return 0;
	}
	if (DataSize<MIN_HEADER_LEN)
		return -1;
	const unsigned char *MaxData=Data+DataSize-MIN_HEADER_LEN;
	const unsigned char *DataEnd=Data+DataSize;
	for (const unsigned char *CurData=Data; CurData<MaxData; CurData++)
	{
		if (IsValidHeader(CurData, DataEnd))
		{
			return (CurData-Data);
		}
	}
	return -1;
}
