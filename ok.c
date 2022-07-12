#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _ts_packet_head
{
	unsigned int sync_byte						:8;
	unsigned int transport_error_indicator		:1;
	unsigned int payload_uint_start_indicator	:1;
	unsigned int transport_priority				:1;
	unsigned int PID							:13;
	unsigned int transport_scrambling_control	:2;
	unsigned int adaptation_field_control		:2;
	unsigned int continuity_counter				:4;

}TS_PACKET_HEAD;

typedef struct _ts_section_head
{
	unsigned int table_id					:8;
	unsigned int section_syntax_indicator	:1;
	unsigned int section_length				:12;
	unsigned int version_number				:5;
	unsigned int current_next_indicator		:1;
	unsigned int section_number				:8;
	unsigned int last_section_number		:8;
}TS_SECTION_HEAD;


int fst_parse(unsigned char* data, unsigned int uiUserPID, unsigned int uiTableId)
{
	unsigned char *buf = data;
	unsigned char *ptr = buf + 8;
	int section_length = ((buf[1] & 0xF) << 8) | buf[2];
	int service_loop_length = section_length - 9;
	
	if(uiTableId != buf[0])
	{
		return -1;
	}
	
	printf("FST\n");
	printf(" |- table_id = 0x%x\n", buf[0]);
	printf(" |- section_syntax_indicator = 0x%x\n", (buf[1] & 0x80) >> 7);
	printf(" |- section_length = 0x%x\n", (buf[1] & 0xF) << 8 | buf[2]);
	printf(" |- original_network_id = 0x%x\n", (buf[3] << 8) | buf[4]);
	printf(" |- version_number = 0x%x\n", (buf[5] & 0x3E) >> 1);
	printf(" |- current_next_indicator = 0x%x\n", buf[5] & 0x1);
	printf(" |- section_number = 0x%x\n", buf[6]);
	printf(" |- last_section_number = 0x%x\n", buf[7]);
	
	int Original_network_id = 0;
	int Transport_stream_id = 0;
	int Service_id = 0;
	int Defautl_video_PID = 0;
	int Defautl_audio_PID = 0;
	int Defautl_video_ECM_PID = 0;
	int Defautl_audio_ECM_PID = 0;
	int Default_PCR_PID = 0;
	int Descriptor_loop_length = 0;
	while(service_loop_length)
	{
		Original_network_id = (ptr[0] << 8) | ptr[1];
		Transport_stream_id = (ptr[2] << 8) | ptr[3];
		Service_id = (ptr[4] << 8) | ptr[5];
		Descriptor_loop_length = ((ptr[16] & 0xF) << 8) | ptr[17];
		printf("   |- onid = 0x%x, tsid = %d, sid = %d\n", Original_network_id, Transport_stream_id, Service_id);
		ptr += (18 + Descriptor_loop_length);
		service_loop_length -= (18 + Descriptor_loop_length);
	}
	
	return 0;
}

int nit_parse(unsigned char* data, unsigned int uiUserPID, unsigned int uiTableId)
{
	unsigned char *buf = data;
	int network_descriptors_length = ((buf[8] & 0xF) << 8) | buf[9];
	int section_number = buf[6];
	int last_section_number = buf[6];
	unsigned char *ptr = buf + 10 + network_descriptors_length;
	int transport_stream_loop_length = ((ptr[0] & 0xF) << 8) | ptr[1];
	ptr += 2;
	
	if(uiTableId != buf[0])
	{
		return -1;
	}
	
	printf("NIT\n");
	printf(" |- table_id = 0x%x\n", buf[0]);
    printf(" |- section_syntax_indicator = 0x%x\n", (buf[1] & 0x80) >> 7);
	printf(" |- section_length = 0x%x\n", (buf[1] & 0xF) << 8 | buf[2]);
	printf(" |- network_id = 0x%x\n", (buf[3] << 8) | buf[4]);
	printf(" |- version_number = 0x%x\n", (buf[5] & 0x3E) >> 1);
	printf(" |- current_next_indicator = 0x%x\n", buf[5] & 0x1);
	printf(" |- section_number = 0x%x\n", buf[6]);
	printf(" |- last_section_number = 0x%x\n", buf[7]);
	
	int transport_stream_id = 0;
	int original_network_id = 0;
	int transport_descriptors_length = 0;
	int transport_stream_length = 0;
	while(transport_stream_loop_length)
	{
		transport_stream_id = (ptr[0] << 8) | ptr[1];
		original_network_id = (ptr[2] << 8) | ptr[3];
		transport_descriptors_length = ((ptr[4] & 0xF) << 8) | ptr[5];
		printf("   |- transport_stream_id = 0x%x, original_network_id = 0x%x\n", transport_stream_id, original_network_id);
		transport_stream_length = 6 + transport_descriptors_length;
		ptr += transport_stream_length;
		transport_stream_loop_length -= transport_stream_length;
	}
	
	return 0;
}


unsigned int GetSectionStart(unsigned char *ucPacketBuffer, TS_PACKET_HEAD *pstPacketHead)
{
	unsigned int uiSectionStart = 0;
 
	switch(pstPacketHead->adaptation_field_control)
	{
		case 0:	break;
		case 1:	uiSectionStart = 4;
				break;
		case 2: break;
		case 3: uiSectionStart = 5 + ucPacketBuffer[4];
			break;
	}
 
	if(pstPacketHead->payload_uint_start_indicator)
	{
		uiSectionStart = uiSectionStart + ucPacketBuffer[uiSectionStart] + 1;
	}
 
	return uiSectionStart;
}

int RestoreSection(unsigned char *ucSectionBuffer, unsigned char *ucPacketBuffer, TS_SECTION_HEAD stSectionHead, TS_PACKET_HEAD *pstPacketHead, int iTsLength, unsigned int *uiSectionPosition)
{
	unsigned int uiSectionEndFlags = 0;
	unsigned int uiCopeSize = 0;
	unsigned int uiSectionStart = GetSectionStart(ucPacketBuffer, pstPacketHead);
 
	if (iTsLength == 204)
	{
		uiCopeSize = iTsLength - uiSectionStart - 16;
		if ((iTsLength - uiSectionStart - 16) > (stSectionHead.section_length + 3 - *uiSectionPosition))
		{
			uiCopeSize = stSectionHead.section_length + 3 - *uiSectionPosition;
			uiSectionEndFlags = 1;
		}
	}
	else
	{
		uiCopeSize = iTsLength - uiSectionStart;
		if ((iTsLength - uiSectionStart) > (stSectionHead.section_length + 3 - *uiSectionPosition))
		{
			uiCopeSize = stSectionHead.section_length + 3 - *uiSectionPosition;
			uiSectionEndFlags = 1;
		}
	}
	
	memcpy(ucSectionBuffer + *uiSectionPosition, ucPacketBuffer + uiSectionStart, uiCopeSize);
	*uiSectionPosition = *uiSectionPosition + uiCopeSize;
 
	return uiSectionEndFlags;
}

int GetSectionHead(unsigned char *ucPacketBuffer, int iTsLength, unsigned int uiTableId, TS_SECTION_HEAD *pstSectionHead, TS_PACKET_HEAD *pstPacketHead)
{
	int iReturn = 0;
	int uiSectionStart = 0;
	uiSectionStart = GetSectionStart(ucPacketBuffer, pstPacketHead);
	if ((uiSectionStart > iTsLength) || (uiSectionStart == 0))
	{
		//printf("uiSectionStart is error\n");
		iReturn = -1;
		goto Ret;
	}
 
	pstSectionHead->table_id = ucPacketBuffer[uiSectionStart];
	if (pstSectionHead->table_id != uiTableId)
	{
		//printf("Get pstSectionHead->table_id error!\n");
		iReturn = -1;
		goto Ret;
	}
	pstSectionHead->section_syntax_indicator = (ucPacketBuffer[uiSectionStart + 1] >> 7) & 0x1;
	pstSectionHead->section_length = ((ucPacketBuffer[uiSectionStart + 1] & 0xf) << 8) | ucPacketBuffer[uiSectionStart + 2];
	pstSectionHead->version_number = (ucPacketBuffer[uiSectionStart + 5] >> 1) & 0x1f;
	pstSectionHead->current_next_indicator = ucPacketBuffer[uiSectionStart + 5] & 0x1;
	pstSectionHead->section_number = ucPacketBuffer[uiSectionStart + 6];
	pstSectionHead->last_section_number = ucPacketBuffer[uiSectionStart + 7];
 
Ret:
	return iReturn;
}


int GetPacketHead(unsigned char *ucPacketBuffer, TS_PACKET_HEAD *pstPacketHead)
{
	int iReturn = 0;
 
	pstPacketHead->sync_byte = ucPacketBuffer[0];
	if (pstPacketHead->sync_byte != 0x47)
	{
		//printf("PacketHead->sync_byte != 0x47\n");
		iReturn = -1;
		goto Ret;
	}
 
	pstPacketHead->transport_error_indicator = ucPacketBuffer[1] >> 7;
	pstPacketHead->payload_uint_start_indicator = (ucPacketBuffer[1] >> 6) & 0x1;
	pstPacketHead->transport_priority = (ucPacketBuffer[1] >> 5) & 0x1;
	pstPacketHead->PID = ((ucPacketBuffer[1] & 0x1f) << 8) | ucPacketBuffer[2];
	pstPacketHead->transport_scrambling_control = ucPacketBuffer[3] >> 6;
	pstPacketHead->adaptation_field_control = (ucPacketBuffer[3] >> 4) & 0x3;
	pstPacketHead->continuity_counter = ucPacketBuffer[3] & 0xf;
 
Ret:
	return iReturn;
}


int GetSection(FILE *pfTsFile, int iTsLength, unsigned int uiUserPID, unsigned int uiTableId, unsigned int *uiVersionNumber, unsigned char *ucSectionBuffer, unsigned int *uiSectionNumberRecord)
{
	unsigned int uiPacketPid = 0;
	unsigned int uiSectionFlags = 0;
	unsigned int uiSectionPosition = 0;
	unsigned char *ucPacketBuffer = (unsigned char *)malloc(sizeof(char) * 204);
	TS_PACKET_HEAD stPacketHead = { 0 };
	TS_SECTION_HEAD stSectionHead = { 0 };
 
	while (feof(pfTsFile) == 0)
	{
		memset(ucPacketBuffer, 0, iTsLength);
		if (-1 == fread(ucPacketBuffer, iTsLength, 1, pfTsFile))
		{
			continue;
		}
 
		//Determine whether this is the PID packet
		uiPacketPid = ((ucPacketBuffer[1] & 0x1f) << 8) | ucPacketBuffer[2];
		if (uiPacketPid == uiUserPID)
		{
			//printf("uiPacketPid = %x\n", uiPacketPid);
			if (-1 == GetPacketHead(ucPacketBuffer, &stPacketHead))
			{
				continue;
			}
 
			//Determine whether there is the first byte of section
			if (stPacketHead.payload_uint_start_indicator == 1)
			{
				//Get the sectionhead
				if (-1 == GetSectionHead(ucPacketBuffer, iTsLength, uiTableId, &stSectionHead, &stPacketHead))
				{
					continue;
				}
 
				//whether the VersionNumber of section is change, if changed return 1;
				if ((*uiVersionNumber != stSectionHead.version_number) && (*uiVersionNumber != 0xff))
				{
					//printf("*uiVersionNumber: 0x%02x, stSectionHead.version_number: 0x%02x\n", *uiVersionNumber, stSectionHead.version_number);
					printf("The version_number is changed!\n");
					continue;
				}
 
				//whether the number of section is we want
				if (uiSectionNumberRecord[stSectionHead.section_number] == 1)
				{
					//printf("the number of section wo have found!\n");
					continue;
				}
 
				//get version number
				if (*uiVersionNumber == 0xff)
				{
					*uiVersionNumber = stSectionHead.version_number;
				}
 
				uiSectionFlags = 1;
				if (1 == RestoreSection(ucSectionBuffer, ucPacketBuffer, stSectionHead, &stPacketHead, iTsLength, &uiSectionPosition))
				{
					//if RestoreSection return 1, have get the section over	
					uiSectionNumberRecord[stSectionHead.section_number] = 1;
					uiSectionNumberRecord[256] += 1;
					break;
				}
			}
 
			//no the first of the section
			else if (uiSectionFlags)
			{
				if (1 == RestoreSection(ucSectionBuffer, ucPacketBuffer, stSectionHead, &stPacketHead, iTsLength, &uiSectionPosition))
				{
					uiSectionNumberRecord[stSectionHead.section_number] = 1;
					uiSectionNumberRecord[256] += 1;
					break;
				}
			}
		}
	}
 
	free(ucPacketBuffer);
	return (uiSectionFlags - 1);
}


int GetTable(FILE *pfTsFile, int iTsPosition, int iTsLength, unsigned int uiUserPID, unsigned int uiTableId)
{
	int iReturn = 0;
	unsigned int uiVersionNumber = 0xff;
	unsigned int uiLastSectionNumber = 0;
	unsigned int uiSectionNumberRecord[257] = { 0 };
	unsigned char *ucSectionBuffer = (unsigned char *)malloc(sizeof(char) * 4096);
 
	if (-1 == fseek(pfTsFile, iTsPosition, SEEK_SET))
	{
		printf("fseek in %d error(GetSection)\n", iTsPosition);
		goto Ret;
	}
 
	while (feof(pfTsFile) == 0)
	{
		memset(ucSectionBuffer, 0, iTsLength);
		iReturn = GetSection(pfTsFile, iTsLength, uiUserPID, uiTableId, &uiVersionNumber, ucSectionBuffer, uiSectionNumberRecord);
		if (iReturn == -1)
		{
			break;
		}
 
		uiLastSectionNumber = ucSectionBuffer[7];
 
		//find function to parse section
		//FindFuncParseSection(ucSectionBuffer, uiUserPID, uiTableId);
		nit_parse(ucSectionBuffer, uiUserPID, uiTableId);
																									
		if (uiSectionNumberRecord[256] > uiLastSectionNumber)
		{
			printf("find one of the table. Please wait to find all the table.\n");
			break;
		}
		
	}
 
Ret:
	free(ucSectionBuffer);
	return iReturn;
}

int ParseTable(FILE *pfTsFile, int iTsPosition, int iTsLength)
{
	unsigned int uiUserPID = 0;
 
	//Get PAT table
	//uiUserPID = 0x0000;
	//GetTable(pfTsFile, iTsPosition, iTsLength, uiUserPID, 0x00);
	
	GetTable(pfTsFile, iTsPosition, iTsLength, 0x0010, 0x41); //NIT
	
	//GetTable(pfTsFile, iTsPosition, iTsLength, 0x03B6, 0xBD); //FST
	
	return 0;
}

int main(int argc, char * argv[])
{
	FILE *fp = fopen(argv[1], "rb");
    ParseTable(fp,0,188);
	close(fp);
	return 0;
}
