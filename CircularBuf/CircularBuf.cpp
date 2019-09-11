#include <ctime>
//#include <chrono>
#include <iostream>
#include <string>
//#include <unistd.h>
//#include <sys/time.h>
#include <string.h>
//#include <pthread.h>
#include <Windows.h>
#include <stdio.h>

//#include "../Services/ServiceUtils.h"

using namespace std;

// A demo instance of Camera module using circular buffer
// 1. Test the circular buffer 
// 2. Test the saving of background recording video files together with main event recordings from single IP camera using circular buffer and two threads structure.
// 3. A sub thread reads the stream from specified camera and saves packet into circular buffer
// 4. The main thread reads packets from circular buffer via two seperqated pointers. One is for background recording. The other one is for main event recording
// 5. Test chunks of recordings

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libavutil/time.h>
}

class CircularBuffer
{
public:
	CircularBuffer();
	CircularBuffer(int max_pts_span, int max_size);
	~CircularBuffer();

	// add a video or audio packet to the circular buffer
	// positive return indicates the packet is added successfully. The number returned is the current total packets in the circular buffer.
	// 0 return indicates that the packet is added successfully but the circular buffer has suffered oversized and been revised.
	int add_packet(AVPacket* pkt);

	// read a packet out of the circular buffer for background recording
	// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
	// 0 return indicates no available packet is read. Or the background reading has reached the end.
	int read_bg_packet(AVPacket* pkt);

	// read a packet out of the circular buffer for main event recording
	// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
	// 0 return indicates no available packet is read. Or the main event reading has reached the end.
	int read_mn_packet(AVPacket* pkt);

	// reset the main event reading pointer to the first available packet
	void reset_mn_read();

protected:
	AVPacketList* first_pkt; // pointer to the first added packet in the circular buffer
	AVPacketList* last_pkt; // pointer to the new added packet in the circular buffer
	AVPacketList* bg_pkt; // the background reading pointer
	AVPacketList* mn_pkt;// the background reading pointer

	int m_TotalPkts; // counter of total packets in the circular buffer
	int m_size;  // total size of the packets in the buffer
	int64_t m_MaxPTSSpan;  // max pts span in the buffer
	int m_MaxSize;

	bool flag_writing; // flag indicates adding new packet to the circular buffer
	bool flag_reading; // flag indicates reading from the circular buffer
};

// global variables
CircularBuffer* cbuf; // global shared the circular buffer
AVFormatContext* ifmt_Ctx = NULL;  // global shared input format context
int64_t lastReadPacktTime; // global shared time stamp for call back
string prefix_videofile = "";

static int interrupt_cb(void* ctx)
{
	int  timeout = 100;
	if (av_gettime() - lastReadPacktTime > timeout * 1000 * 1000)
	{
		return -1;
	}
	return 0;
}

// This is the sub thread that captures the video streams from specified IP camera and saves them into the circular buffer
//void* videoCapture(void* myptr)
DWORD WINAPI videoCapture(LPVOID myPtr)
{
	AVPacket pkt;

	int64_t pts0 = 0;
	int64_t wclk0 = 0;
	int index_video = -1;
	int ret;

	// find the index of video stream
	for (int i = 0; i < ifmt_Ctx->nb_streams; i++)
	{
		if (ifmt_Ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			index_video = i;
			break; // break the for loop
		}
	}
	if (index_video < 0)
	{
		fprintf(stderr, "Cannot find video stream in the camera.\n");
		exit(1);
	}
	AVRational in_stream_time_base = ifmt_Ctx->streams[index_video]->time_base; //time base of video stream from the IP camera
	int32_t factor_num = in_stream_time_base.num * 1000;  // factor to adjust the time base of camera stream to wall clock time base
	int32_t factor_den = in_stream_time_base.den * 1; // factor to adjust the time base of camera stream to wall clock time base

	// read packets from IP camera and save it into circular buffer
	while (true)
	{
		ret = av_read_frame(ifmt_Ctx, &pkt); // read a frame from the camera
		
		// save those video stream only
		if (ret < 0)
			break;

		if (pkt.pts == AV_NOPTS_VALUE || pkt.stream_index != index_video)
		{
			// add blue screen here
			av_packet_unref(&pkt);
			continue;
		}

		// Store the starting point of pts and wall clock
		int64_t wclk = av_gettime() / 1000; // current wall clock in miliseconds, epoch time
		if (pts0 == 0)
		{
			pts0 = pkt.pts; // origin of pts
			wclk0 = wclk; // origin of wall clock
			fprintf(stderr, "Origin of the stream %lld %lld.\n", pts0, wclk0);
		}

		pkt.pts = (pkt.pts - pts0) * factor_num / factor_den + wclk0;
		//pkt.dts = (pkt.dts - pts0) * factor_num / factor_den + wclk0;
		pkt.dts = pkt.pts;
		if (pkt.duration > 0)
			pkt.duration = pkt.duration * factor_num / factor_den;
		else
			pkt.duration = 0;

		// add the reading packet to circular buffer
		fprintf(stderr, "%lldms: Add packet (%lld, %d). ",
			wclk - wclk0, pkt.pts - wclk0, pkt.size);
		ret = cbuf->add_packet(&pkt);  // add the read packet to the circular buffer
		if (ret > 0)
			fprintf(stderr, "Circular buffer has %d packets now.\n", ret);
		else
			fprintf(stderr, "Circular buffer is now full.\n");
		av_packet_unref(&pkt); // handle the release of the packet here
	}
}

char* av_err(int ret)
{
	// buffer to store error messages
	static char buf[256];
	if (ret < 0)
		av_strerror(ret, buf, sizeof(buf));
	else
		memset(buf, 0, sizeof(buf));

	return buf;
}

int main(int argc, char** argv)
{
	// The IP camera
	string CameraPath = "rtsp://10.25.50.20/h264";
	//string CameraPath = "rtsp://10.0.9.113:8554/0";
	string filename_bg = ""; // file name of background recording
	string filename_mn = ""; // file name of main recording

	if (argc > 1)
		CameraPath.assign(argv[1]);

	int ret = avformat_network_init();
	ifmt_Ctx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options, "buffer_size", "200000", 0); // 200k for 1080p
	av_dict_set(&options, "rtsp_transport", "tcp", 0); // using tcp for rtsp
	av_dict_set(&options, "stimeout", "100000", 0); // set timeout to be 100ms, in micro second
	av_dict_set(&options, "max_delay", "500000", 0); // set max delay to be 500ms

	//ifmt_Ctx->interrupt_callback.callback = interrupt_cb;
	ret = avformat_open_input(&ifmt_Ctx, CameraPath.c_str(), 0, &options);
	if (ret < 0)
	{
		fprintf(stderr, "Could not open IP camera at %s with error %s.\n", CameraPath.c_str(), av_err(ret));
		exit(1);
	}

	ret = avformat_find_stream_info(ifmt_Ctx, 0);
	if (ret < 0)
	{
		fprintf(stderr, "Could not find stream information from IP camera at %s with error %s.\n", CameraPath.c_str(), av_err(ret));
		exit(1);
	}

	// Debug only, output the camera information
	av_dump_format(ifmt_Ctx, 0, CameraPath.c_str(), 0);

	// Open a circular buffer
	cbuf = new CircularBuffer(30 * 1000, 100 * 1000 * 1000); // 30s and 100M

	// assign the output format. It is used for background recordings
	AVFormatContext* ofmt_Ctx = NULL;
	avformat_alloc_output_context2(&ofmt_Ctx, NULL, "mp4", NULL);
	if (!ofmt_Ctx)
	{
		fprintf(stderr, "Could not create output context.");
		exit(1);
	}
	AVOutputFormat* ofmt = ofmt_Ctx->oformat;

	for (int i = 0; i < ifmt_Ctx->nb_streams; i++)
	{
		AVStream* out_stream = avformat_new_stream(ofmt_Ctx, NULL);
		if (!out_stream)
		{
			fprintf(stderr, "Failed allocating output stream.");
			exit(1);
		}

		// copy the IP camera stream parameters to the video file stream
		ret = avcodec_parameters_copy(out_stream->codecpar, ifmt_Ctx->streams[i]->codecpar);
		if (ret < 0)
		{
			fprintf(stderr, "Failed to copy codec parameters.");
			exit(1);
		}
		//out_stream->codecpar->codec_tag = 0;
		out_stream->time_base.num = 1;
		out_stream->time_base.den = 16000; // wall clock time base is 1/1000
	}

	int64_t ChunkTime_bg = 0;  // Chunk time for background recording
	int64_t ChunkTime_mn = 0;
	int64_t CurrentTime;
	int number_bg = 0;


	// Start a seperate thread to capture video stream from the IP camera
	//pthread_t thread;
	//ret = pthread_create(&thread, NULL, videoCapture, NULL);
	DWORD myThreadID;
	HANDLE myHandle = CreateThread(0, 0, videoCapture, 0, 0, &myThreadID);
	if (myThreadID == 0)
	{
		fprintf(stderr, "Cannot create the timer thread.");
		exit(1);
	}
	av_usleep(5*1000*1000);

	AVDictionary* dictionary = NULL;
	av_dict_set(&dictionary, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	AVPacket pkt;
	int64_t pts0 = 0;

	while (true)
	{
		CurrentTime = av_gettime() / 1000;  // read current time in miliseconds
		if (CurrentTime > ChunkTime_bg)
		{
			//ChunkTime_bg = (CurrentTime / 3600000 + 1) * 3600000; // set next chunk time at x:00:00, chunk = 3600s
			//ChunkTime_bg = (CurrentTime / 600000 + 1) * 600000; // set next chunk time at x:x0:00, chunk = 600s
			ChunkTime_bg = (CurrentTime / 60000 + 1) * 60000; // set next chunk time at x:xx:00, chunk = 60s

			// Close the previous output
			if (filename_bg != "")
			{
				av_write_trailer(ofmt_Ctx);
				if (ofmt_Ctx && !(ofmt->flags & AVFMT_NOFILE))
					avio_closep(&ofmt_Ctx->pb);
				fprintf(stderr, "Chunked video file saved.\n");
			}
			else
				fprintf(stderr, "First background recording.\n");

			// Open a new chunk
			filename_bg = prefix_videofile + to_string(number_bg++) + ".mp4";
			av_dump_format(ofmt_Ctx, 0, filename_bg.c_str(), 1);

			if (!(ofmt->flags & AVFMT_NOFILE))
			{
				ret = avio_open(&ofmt_Ctx->pb, filename_bg.c_str(), AVIO_FLAG_WRITE);
				if (ret < 0)
				{
					fprintf(stderr, "Could not open output file %sn", filename_bg.c_str());
					break;
				}
			}

			ret = avformat_write_header(ofmt_Ctx, &dictionary);
			if (ret < 0)
			{
				fprintf(stderr, "Could not open %s\n", filename_bg.c_str());
				break;;
			}
		}

		// read a background packet from the queue
		ret = cbuf->read_bg_packet(&pkt);
		if (ret > 0)
		{
			if (pts0 == 0)
				pts0 = pkt.pts;
			fprintf(stderr, "Read a packet (%lld, %d), %d packets left in circular buffer.\n",
				pkt.pts - pts0, pkt.size, ret);

			pkt.pos = -1;
			ret = av_interleaved_write_frame(ofmt_Ctx, &pkt);
			av_packet_unref(&pkt);

			if (ret < 0)
			{
				fprintf(stderr, "Error muxing packet in %s.\n",	filename_bg.c_str());
				break;
			}
		}
		else
		{
			//fprintf(stderr, "No more packets for background recording.\n");
			av_usleep(1000 * 10); // sleep for 10ms
		}


	}

	if (ret < 0)
		fprintf(stderr, " with error %s.\n", av_err(ret));

	// Close output
	if (filename_bg != "" && !(ofmt_Ctx->flags & AVFMT_NOFILE))
		avio_closep(&ofmt_Ctx->pb);
	avformat_free_context(ofmt_Ctx);
}

CircularBuffer::CircularBuffer()
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_TotalPkts = 0;
	m_size = 0;
	m_MaxPTSSpan = 0;
	m_MaxSize = 0;
	
	flag_writing = false;
	flag_reading = false;
}

CircularBuffer::CircularBuffer(int max_pts_span, int max_size)
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_TotalPkts = 0;
	m_size = 0;
	m_MaxPTSSpan = 0;
	m_MaxSize = 0;

	flag_writing = false;
	flag_reading = false;

	if (max_pts_span > 0)
		m_MaxPTSSpan = max_pts_span;

	if (max_size > 0)
		m_MaxSize = max_size;
}

CircularBuffer::~CircularBuffer()
{
	while (first_pkt)
	{
		AVPacketList* pktl = first_pkt;
		av_packet_unref(&pktl->pkt);
		first_pkt = pktl->next;
		av_free(pktl);
	}
}

// add a video or audio packet to the circular buffer
// positive return indicates the packet is added successfully. The number returned is the current total packets in the circular buffer.
// 0 return indicates that the packet is added successfully but the circular buffer has suffered oversized and been revised.
// negative return indicates that the packet is not added due to run out of memory
int CircularBuffer::add_packet(AVPacket* pkt)
{
	// Not allowed empty packet in circular buffer
	if (!pkt)
		return 0;

	// Ensure the packet is reference counted
	if (av_packet_make_refcounted(pkt) < 0)
	{
		av_packet_unref(pkt);
		return 0;
	}

	AVPacketList* pktl = (AVPacketList*)av_mallocz(sizeof(AVPacketList));
	if (!pktl)
	{
		av_packet_unref(pkt);
		av_free(pktl);
		return 0;
	}

	// set the writing flag to block unsafe reading
	flag_writing = true;

	// add the packet to the queue
	av_packet_ref(&pktl->pkt, pkt);  // leave the pkt alone
	//av_packet_move_ref(&pktl->pkt, pkt);  // this makes the pkt unref
	pktl->next = NULL;

	// modify the pointers
	if (!last_pkt)
		first_pkt = pktl; // handle the first adding
	else
		last_pkt->next = pktl;
	last_pkt = pktl; // the new added packet is always the last packet in the circular buffer

	m_TotalPkts++;
	m_size += pktl->pkt.size + sizeof(*pktl);

	// do not update the pointers while actively reading
	if (flag_reading)
	{
		flag_writing = false;
		return m_TotalPkts;
	}

	if (!bg_pkt)
		bg_pkt = last_pkt;

	if (!mn_pkt)
		mn_pkt = last_pkt;

	// revise the circular buffer by kicking out those overflowed packets
	int64_t allowed_pts = last_pkt->pkt.pts - m_MaxPTSSpan;
	int64_t pts_bg = bg_pkt->pkt.pts;
	int64_t pts_mn = mn_pkt->pkt.pts;
	while (first_pkt->pkt.pts < allowed_pts)
	{
		pktl = first_pkt;
		m_TotalPkts--; // reduce the number of total packets
		m_size -= first_pkt->pkt.size + sizeof(*first_pkt);

		av_packet_unref(&first_pkt->pkt); // unref the first packet
		first_pkt = first_pkt->next;
		av_freep(&pktl);  // free the unsed packet list
	}

	if (pts_bg < first_pkt->pkt.pts)
		bg_pkt = first_pkt;

	if (pts_mn < first_pkt->pkt.pts)
		mn_pkt = first_pkt;

	flag_writing = false;
	return m_TotalPkts;
}

// read a packet out of the circular buffer for background recording
// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
// 0 return indicates no available packet is read. Or the background reading has reached the end.
int CircularBuffer::read_bg_packet(AVPacket* pkt)
{
	// no reading while adding new packet
	if (flag_writing)
		return 0;

	flag_reading = true;
	int ret = 0;
	if (bg_pkt)
	{
		//*pkt = bg_pkt->pkt;  // this will make the pkt expose to outside
		av_packet_ref(pkt, &bg_pkt->pkt); // expose to the outside a copy of the packet
		bg_pkt = bg_pkt->next;
		ret = m_TotalPkts; // return the number of total packets
	}

	flag_reading = false;
	return ret; 
};

// read a packet out of the circular buffer for main event recording
// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
// 0 return indicates no available packet is read. Or the main event reading has reached the end.
int CircularBuffer::read_mn_packet(AVPacket* pkt)
{
	// if the buffer is empty
	if (!last_pkt)
	{
		first_pkt = NULL;
		bg_pkt = NULL;
		mn_pkt = NULL;
		return 0;
	}

	AVPacketList* pktl = mn_pkt;
	if (pktl)
	{
		// handle the zombie mn_pkt pointer
		if (pktl->pkt.pts < first_pkt->pkt.pts || pktl->pkt.pts > last_pkt->pkt.pts)
		{
			mn_pkt = first_pkt;
			return 0;
		}

		mn_pkt = pktl->next;
		*pkt = pktl->pkt;

		// revise the circular buffer by kicking out those overflowed packets
		while (last_pkt->pkt.pts - first_pkt->pkt.pts > m_MaxPTSSpan)
		{
			// revise till reaching mn_pkt
			if (first_pkt == pktl)
				break;

			av_packet_unref(&first_pkt->pkt); // unref the first packet
			if (first_pkt == bg_pkt)
			{
				first_pkt = first_pkt->next; // move the pointer of the first packet
				bg_pkt = first_pkt; // reset the background reading pointer
			}
			else
				first_pkt = first_pkt->next; // move the pointer of the first packet
			m_TotalPkts--; // reduce the number of total packets
		}

		return m_TotalPkts; // return the number of total packets
	}

	pkt = NULL; // no available packet
	return 0; // no available packet
};

// reset the main read
void CircularBuffer::reset_mn_read()
{
	// reset the main packet to the first packet. This method is called when starting a new main event video recording.
	mn_pkt = first_pkt;
};