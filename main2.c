/*
 *  parse dvb ts psi/si table
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*p_table_parse)(unsigned char* buf, int length, int pid, int table_id);

typedef struct _program
{
	int program_number;
	int program_map_pid;
}program_t;

typedef struct _transport
{
	int tsid;
	int program_size;
	program_t programs[256];
	int network_size;
	int network[256];
}transport_t;

transport_t transport;
int section_over = 0;
int section_number_record[255] = {0};
int section_number_max = 0;
int section_number_record_count = 0;
int section_end_flag = 0;

typedef struct _ts_packet
{
	unsigned int sync_byte						:8;
	unsigned int transport_error_indicator		:1;
	unsigned int payload_unit_start_indicator	:1;
	unsigned int transport_priority				:1;
	unsigned int pid							:13;
	unsigned int transport_scrambling_control	:2;
	unsigned int adaptation_field_control		:2;
	unsigned int continuity_counter				:4;
	
	unsigned int adaptation_field_length		:8;
	unsigned int pointer_field					:8;
	unsigned int payload_offset					;
}ts_packet_t;

int ts_packet_parse(ts_packet_t * tspkt, unsigned char data[], int packet_size)
{
	unsigned char * ptr = data;
	tspkt->payload_offset = 0;
	tspkt->sync_byte = ptr[0];
	tspkt->transport_error_indicator = (ptr[1] & 0x80) >> 7;
	tspkt->payload_unit_start_indicator = (ptr[1] & 0x40) >> 6;
	tspkt->transport_priority = (ptr[1] & 0x20) >> 5;
	tspkt->pid = ((ptr[1] & 0x1F) << 8) | ptr[2];
	tspkt->transport_scrambling_control = (ptr[3] & 0xC0) >> 6;
	tspkt->adaptation_field_control = (ptr[3] & 0x30) >> 4;
	tspkt->continuity_counter = ptr[3] & 0xF;
	
	if(0x47 != tspkt->sync_byte || 1 == tspkt->transport_error_indicator || tspkt->transport_scrambling_control)
	{
		//ts packet sync_byte总是0x47
		//transport_error_indicator被置1表明至少有一个不正确的位错误存在
		//transport_scrambling_control不等于0表示被加扰，传输流包头和自适应区域（如果存在）不应被加扰
		return -1;
	}
	
	ptr += 4;
	tspkt->payload_offset += 4;
	
	if(0x00 == tspkt->adaptation_field_control || 0x02 == tspkt->adaptation_field_control)
	{
		//没有payload
		tspkt->adaptation_field_length = ptr[0]; //adaptation_field长度占用1个字节
		tspkt->payload_offset = 0;
		return 0;
	}
	if(0x03 == tspkt->adaptation_field_control)
	{
		//同时存在adaptation_field和payload
		tspkt->adaptation_field_length = ptr[0]; //adaptation_field长度占用1个字节
		//需要跳过adaptation_field_length字节占据的1字节，还要跳过adaptation_field占用的字节
		tspkt->payload_offset += (tspkt->adaptation_field_length + 1);
		ptr += (tspkt->adaptation_field_length + 1);
	}
	// 0x1 == tspkt->adaptation_field_control //只有payload
	if(1 == tspkt->payload_unit_start_indicator) //如果是起始section
	{
		tspkt->pointer_field = ptr[0];
		//除了跳过pointer_field占据的1字节，还要跳过pointer_field描述的字节，才是section的开始
		tspkt->payload_offset += (tspkt->pointer_field + 1);
		ptr += (tspkt->pointer_field + 1);
	}
	
	return 0;
}

int fst_parse(unsigned char* data, int length, int pid, int table_id)
{
	unsigned char *buf = data;
	unsigned char *ptr = buf + 8;
	int section_length = ((buf[1] & 0xF) << 8) | buf[2];
	int service_loop_length = section_length - 9;
	
	if(table_id != buf[0])
	{
		return -1;
	}
	
	if(1 == section_number_record[buf[6]]) //这个section已经处理过
	{
		return 0;
	}
	section_number_max = buf[7];
	section_number_record[buf[6]] = 1;
	
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

int nit_parse(unsigned char* data, int length, int pid, int table_id)
{
	unsigned char *buf = data;
	int network_descriptors_length = ((buf[8] & 0xF) << 8) | buf[9];
	int section_number = buf[6];
	int last_section_number = buf[6];
	unsigned char *ptr = buf + 10 + network_descriptors_length;
	int transport_stream_loop_length = ((ptr[0] & 0xF) << 8) | ptr[1];
	ptr += 2;
	
	if(table_id != buf[0])
	{
		return -1;
	}
	//TODO
	/*
	for(int i=0;i<last_section_number+1;i++)
	{
		if(1 == section_number_record[section_number])
		{
			return 0;
		}
	}
	*/
	/*
	if(1 == section_number_record[buf[6]]) //这个section已经处理过
	{
		return 0;
	}
	section_number_max = buf[7];
	section_number_record[buf[6]] = 1;
	*/
	
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

int pmt_parse(unsigned char* data, int length, int pid, int table_id)
{
	unsigned char *ptr = NULL;
	unsigned char *buf = data;
	int section_length = ((buf[1] & 0xF) << 8) | buf[2];
	int program_info_length = ((buf[10] & 0xF) << 8) | buf[11]; //N循环，描述符descriptor长度
	int stream_loop_length = section_length - program_info_length - 13; //N1循环
	
	if(0x2 != buf[0]) //TS_program_map_section table_id总是0x2
	{
		return -1;
	}
	printf("PMT\n");
	printf(" |- table_id = 0x%x\n", buf[0]);
    printf(" |- section_syntax_indicator = 0x%x\n", (buf[1] & 0x80) >> 7);
    printf(" |- section_length = 0x%x\n", ((buf[1] & 0xF) << 8) | buf[2]);
    printf(" |- program_number = 0x%x\n", (buf[3] << 8) | buf[4]);
    printf(" |- version_number = 0x%x\n", (buf[5] & 0x3E) >> 1);
    printf(" |- current_next_indicator = 0x%x\n", buf[5] & 0x1);
    printf(" |- section_number = 0x%x\n", buf[6]);
    printf(" |- last_section_number = 0x%x\n", buf[7]);
	printf(" |- PCR_PID = 0x%x\n", ((buf[8] & 0x1F) << 8) | buf[9]);
	printf(" |- program_info_length = 0x%x\n", ((buf[10] & 0xF) << 8) | buf[11]);
	
	ptr = buf + 12 + program_info_length; //来到N1这个loop
	
	int stream_type = 0;
	int elementary_PID = 0;
	int ES_info_length = 0;
	int stream_length = 0;
	while(stream_loop_length)
	{
		stream_type = ptr[0];
		elementary_PID = ((ptr[1] & 0x1F) << 8) | ptr[2];
		ES_info_length = ((ptr[3] & 0xF) << 8) | ptr[4];
		printf("   |- elementary_PID = 0x%x, stream_type = 0x%x, ES_info_length = 0x%x\n", elementary_PID, stream_type, ES_info_length);
		stream_length = 5 + ES_info_length;
		ptr += stream_length;
		stream_loop_length -=  stream_length;
	}
	
	return 0;
}

int pat_parse(unsigned char* data, int length, int pid, int table_id)
{
	int program_loop_number = 0;
	int program_loop_pid = 0;
	unsigned char *buf = data;
	int programs_size = length - 12; //所有pramgram的长度总和
	int i = 0;
	
	if(table_id != buf[0])
	{
		return -1;
	}
	
	printf("PAT\n");
	printf(" |- table_id = 0x%x\n", buf[0]);
    printf(" |- section_syntax_indicator = 0x%x\n", (buf[1] & 0x80) >> 7);
    printf(" |- section_length = 0x%x\n", ((buf[1] & 0xF) << 8) | buf[2]);
    printf(" |- transport_stream_id = 0x%x\n", (buf[3] << 8) | buf[4]);
    printf(" |- version_number = 0x%x\n", (buf[5] & 0x3E) >> 1);
    printf(" |- current_next_indicator = 0x%x\n", buf[5] & 0x1);
    printf(" |- section_number = 0x%x\n", buf[6]);
    printf(" |- last_section_number = 0x%x\n", buf[7]);
	
	transport.tsid = (buf[3] << 8) | buf[4];
	
	buf += 8; //跳过8个字节，来到program_loop
	while(programs_size > 0)
	{
		program_loop_number = buf[0] << 8 | buf[1];
		program_loop_pid = ((buf[2] & 0x1F) << 8) | buf[3];
		if(0 == program_loop_number)
		{
			printf("   |- program_number = 0x%04x, network_PID = 0x%04x\n", program_loop_number, program_loop_pid);
		}
		else
		{
			transport.program_size++;
			transport.programs[i].program_number = program_loop_number;
			transport.programs[i].program_map_pid = program_loop_pid;
			i++;
			printf("   |- program_number = 0x%04x, program_map_PID = 0x%04x\n", program_loop_number, program_loop_pid);
		}
		buf += 4;
		programs_size -= 4;
	}
	return 0;
}

int psi_section(char * file, int start_position, int packet_size, int pid, int table_id, p_table_parse table_parse)
{
	ts_packet_t tspkt;
	unsigned char buf[256] = {0};
	unsigned char *section_data = (unsigned char *)malloc(sizeof(unsigned char)*4096);
	int section_data_payload_size = 0;
	int section_length = 0;
	int copy_flag = 0;
	int ret = 0;
	
	FILE * fp = fopen(file, "rb");
	if(NULL == fp)
    {
        printf("failed to open %s\n", file);
        return -1;
    }
	
	if(0 != start_position)
	{
		fseek(fp, start_position, SEEK_SET);
	}

	while(!feof(fp))
	{
		memset(buf, 0, sizeof(unsigned char)*256);
		fread(buf, 1, packet_size, fp);
		memset(&tspkt, 0, sizeof(ts_packet_t));
		ret = ts_packet_parse(&tspkt, buf, packet_size);
		if(tspkt.pid != pid || -1 == ret)
		{
			continue;
		}
		if(1 == tspkt.payload_unit_start_indicator)
		{
			section_end_flag = 0;
			copy_flag = 1;
			section_length = ((section_data[1] & 0xf) << 8) | section_data[2] + 3;
			//if(188 - tspkt.payload_offset > section_length - section_data_payload_size)
			if(section_data_payload_size > section_length)
			{
				section_end_flag = 1;
				//section end
			}
			memcpy(section_data + section_data_payload_size, buf + tspkt.payload_offset, 188 - tspkt.payload_offset);
			section_data_payload_size += (188 - tspkt.payload_offset);
		}
		else if (1 == copy_flag)
		{
			memcpy(section_data + section_data_payload_size, buf + tspkt.payload_offset, 188 - tspkt.payload_offset);
			section_data_payload_size += (188 - tspkt.payload_offset);
		}
		if(section_end_flag)
		{
			table_parse(section_data, section_length, tspkt.pid, table_id);
			memset(section_data,0,sizeof(unsigned char)*4096);
			section_data_payload_size = 0;
			section_length = 0;
			section_end_flag = 0;
			copy_flag = 0;
		}
/*
		if(1 == tspkt.payload_unit_start_indicator) //找到起始section
		{
			if (1 == copy_flag)
			{
				copy_flag = 2;
				section_length = ((section_data[1] & 0xf) << 8) | section_data[2] + 3;
				printf("section_length = %d\n",section_length);
				//section_length描述的长度，加上前面table_id、section_syntax_indicator、'0'、reserved和section_length自身的长度，才是整个section的长度
				if(section_data_payload_size > section_length)
				{
					section_data_payload_size = section_length;
					//break;
					section_over = 1; //组完一个section
					printf("section_data_payload_size = %d\n",section_data_payload_size);
				}
			}
			if(0 == copy_flag)
			{
				copy_flag = 1;
			}
		}
		if(1 == copy_flag)
		{
			memcpy(section_data + section_data_payload_size, buf + tspkt.payload_offset, 188 - tspkt.payload_offset);
			section_data_payload_size += 188 - tspkt.payload_offset;
		}
		if (1 == section_over) //组完一个section，解析完后，把数据和标志重新置位，重新组剩余section
		{
			table_parse(section_data, section_data_payload_size, tspkt.pid, table_id);
			section_data_payload_size = 0;
			memset(section_data,0,sizeof(unsigned char)*4096);
			copy_flag = 0;
			//section_over = 0;
		}
*/
	}
	//table_parse(section_data, section_data_payload_size, tspkt.pid);
/*	
	if(NULL != section_data)
	{
		free(section_data);
	}
*/	
	return 0;
}

int get_packet_size(FILE * fp, int * start_position)
{
    int count1 = 100;
    int count2 = 0;
    unsigned char packet[256] = {0};
    fseek(fp, *start_position, SEEK_SET);
    while(!feof(fp)&&count1--)
    {
        fread(packet,1,188,fp);
        if(0x47 == packet[0])
        {
            count2++;
        }
        if(99 == count2)
        {
            return 188;
        }
    }
    return 0;
}

int ts_packet_probe(char * file, int * start_position, int * packet_size)
{
	FILE * fp = fopen(file, "rb");
    if(NULL == fp)
    {
        printf("failed to open %s\n", file);
        return -1;
    }
    unsigned char sync_byte[1] = {0};
    while(!feof(fp))
    {
        fread(sync_byte, 1, 1, fp);
        if(0x47 == sync_byte[0])
        {
            *start_position = ftell(fp) - 1;
            *packet_size = get_packet_size(fp, start_position);
            if(*packet_size)
            {
                break;
            }
        }
    }
	fclose(fp);
	return 0;
}

int main(int argc, char * argv[])
{
    char * file = argv[1];
    int start_position = 0;
    int packet_size = 0;

    if(2 != argc)
    {
        printf("usage example:\n");
		printf("./main cctv.ts\n");
        return 0;
    }

    ts_packet_probe(file, &start_position, &packet_size);
    if(!packet_size)
    {
        printf("can not get packet size\n");
        return 0;
    }
    //printf("%s: start_position = %d, packet_size = %d\n", file, start_position, packet_size);

	//psi_section(file, start_position, packet_size, 0x0000, 0x00, pat_parse);
	//for(int i=0;i<transport.program_size;i++)
	//{
	//	psi_section(file, start_position, packet_size, transport.programs[i].program_map_pid, 0x02, pmt_parse);
	//}

	psi_section(file, start_position, packet_size, 0x0010, 0x40, nit_parse);
	//psi_section(file, start_position, packet_size, 0x0010, 0x41, nit_parse);
	
	//psi_section(file, start_position, packet_size, 0x03B6, 0xBD, fst_parse);

    return 0;
}
