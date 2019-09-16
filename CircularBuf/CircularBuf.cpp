#include <ctime>
#include <iostream>
#include <string>
#include <string.h>
#include <stdio.h>
#include <Windows.h>
//#include <pthread.h>

#define ALIGN_TO_WALL_CLOCK 1

// A demo instance of Camera module using circular buffer
// 1. Test the circular buffer 
// 2. Test the saving of background recording video files together with main event recordings from single IP camera using circular buffer and two threads structure.
// 3. A sub thread reads the stream from specified camera and saves packet into circular buffer
// 4. The main thread reads packets from circular buffer via two seperqated pointers. One is for background recording. The other one is for main event recording
// 5. Test chunks of recordings

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/timestamp.h>
#include <libavutil/time.h>
}

class CircularBuffer
{
public:
	CircularBuffer();
	CircularBuffer(int time_span, int max_size);
	~CircularBuffer();

	// Add the stream into the circular buffer
	//int add_stream(AVFormatContext* ifmt_Ctx, int stream_index = 0);
	int add_stream(AVStream * stream, int stream_index = 0);

	// set the operation option
	// option = CHANGE_STREAM_INDEX, change the stream index of every packet to 0
	// option = KEEP_STREAM_INDEX, keep the stream index unchanged
	void set_option(int option);

	// push or add a packet to the circular buffer
	// The pkt.stream_index is changed to 0
	// positive return indicates the packet is added successfully. The number returned is the current total packets in the circular buffer.
	// 0 return indicates that the packet is added successfully but the circular buffer has suffered oversized and been revised.
	int push_packet(AVPacket* pkt);

	// read a packet out of the circular buffer.
	// read a packet using the background reader when isBackground is true
	// read a packet using the main reader when isBackground is false
	// a positive return indicates the packet is read. 
	int peek_packet(AVPacket* pkt, bool isBackground=true);

	// reset the main reader to very beginning
	void reset_main_reader();

	// get the stream codec parameter
	AVCodecParameters* get_stream_codecpar();

	// get the stream time base
	AVRational get_time_base();

	// get the circular buffer size
	int get_size();

	// get the error message of last operation
	std::string get_error_message();

protected:
	AVPacketList* first_pkt; // pointer to the first added packet in the circular buffer
	AVPacketList* last_pkt; // pointer to the new added packet in the circular buffer
	AVPacketList* bg_pkt; // the background reading pointer
	AVPacketList* mn_pkt; // the main reading pointer
	AVCodecParameters* m_codecpar; // The codec parameters of the bind stream

	int m_TotalPkts; // counter of total packets in the circular buffer
	int m_size;  // total size of the packets in the buffer
	int m_time_span;  // max time span in seconds
	int64_t m_pts_span; // pts span
	int64_t m_wclk_offset; // the pts offset to the wall clock
	AVRational m_time_base; // the time base of the bind stream
	int m_stream_index; // the desired stream index
	int m_MaxSize; // the maximum size allowed for the circular buffer 
	int m_option; // operation options

	int m_err; // the error code of last operation
	std::string m_message; // the error message of last operation

	bool flag_writing; // flag indicates adding new packet to the circular buffer
	bool flag_reading; // flag indicates reading from the circular buffer
};

// global variables
CircularBuffer* cbuf; // global shared the circular buffer
AVFormatContext* ifmt_Ctx = NULL;  // global shared input format context
int64_t lastReadPacktTime = 0; // global shared time stamp for call back
std::string prefix_videofile = "C:\\Users\\georges\\Documents\\CopTraxTemp\\";
bool Debug = true;

class VideoRecorder
{
public:
	VideoRecorder();
	~VideoRecorder();

	// intialize the recorder according to input format context
	//int add_stream(AVFormatContext* ifmt_Ctx, int stream_index=0);
	//int add_stream(AVStream* stream, int stream_index = 0);
	int add_stream(AVStream* stream);

	// open a video recorder specified by url, can be a filename or a rtp url
	int open(std::string url);  

	// close the video recorder
	int close();

	// save the packet to the video recorder
	// the stream index specify the audio or video
	int record(AVPacket* pkt, int stream_index = 0); 

	// set the options for video recorder, has to be called before open
	int set_options(std::string option, std::string value); 

	// get the output format context
	AVFormatContext* get_output_format_context();

	// get the stream time base
	AVRational get_stream_time_base(int stream_index = 0);

	// get the error message of last operation
	std::string get_error_message();

protected:
	std::string m_url;
	AVFormatContext* m_ofmt_Ctx;
	AVDictionary* m_options;
	int m_index_video;
	int m_index_audio;
	bool m_flag_interleaved;

	AVRational m_tbf_video; // the factor to rescale the packet time stamp to fit output video stream
	AVRational m_tbf_audio; // the factor to rescale the packet time stamp to fit output audio stream
	AVRational m_time_base_audio;  // the time base of input audio stream
	AVRational m_time_base_video;  // the time base of input video stream

	int64_t m_pts_offset_video;
	int64_t m_pts_offset_audio;
	int64_t m_defalt_duration_audio;
	int64_t m_defalt_duration_video;

	int m_err; // the error code of last operation
	std::string m_message; // the error message of last operation
};

static int interrupt_cb(void* ctx)
{
	int64_t  timeout = 100 * 1000 * 1000;
	if (av_gettime() - lastReadPacktTime > timeout)
	{
		return -1;
	}
	return 0;
};

// This is the sub thread that captures the video streams from specified IP camera and saves them into the circular buffer
// void* videoCapture(void* myptr)
DWORD WINAPI videoCapture(LPVOID myPtr)
{
	AVPacket pkt;

	int64_t pts0 = 0;
	int64_t wclk0 = 0;
	int64_t pts_offset = 0;
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

	// get the stream time base
	AVRational tb = ifmt_Ctx->streams[index_video]->time_base;
	tb.num *= 1000; // change the time base to be ms based

	// read packets from IP camera and save it into circular buffer
	while (true)
	{
		ret = av_read_frame(ifmt_Ctx, &pkt); // read a frame from the camera
		
		// handle the timeout, blue screen shall be added here
		if (ret < 0)
		{
			fprintf(stderr, "Timeout when read from the IP camera.\n");
			continue;
		}

		ret = cbuf->push_packet(&pkt);  // add the packet to the circular buffer
		if (ret > 0)
		{
			fprintf(stderr, "Added a new packet (%lldms, %d). The circular buffer has %d packets with size %d now.\n",
				pkt.pts * tb.num / tb.den, pkt.size, ret, cbuf->get_size());
		}
		else
		{
			ret = 0;
			if (ret == -1)
			{
				fprintf(stderr,	"A null packet is not allowed in this circular buffer.\n");
			}
			else if (ret == -2)
			{
				fprintf(stderr, "A packet with different stream index is not allowed in this circular buffer.\n");
			}
			else if (ret == -3)
			{
				fprintf(stderr, "A packet that cannot be referenced is not allowed.\n");
			}
			else if (ret == -4)
			{
				fprintf(stderr, "A packet that has no pts is not allowed.\n");
			}
		}
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
	//std::string CameraPath = "rtsp://10.25.50.20/h264";
	std::string CameraPath = "rtsp://10.0.9.113:8554/0";
	//std::string CameraPath = "rtsp://10.0.0.18/h264";
	std::string filename_bg = ""; // file name of background recording
	std::string filename_mn = ""; // file name of main recording

	if (argc > 1)
		CameraPath.assign(argv[1]);

	int ret = avformat_network_init();
	ifmt_Ctx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options, "buffer_size", "200000", 0); // 200k for 1080p
	av_dict_set(&options, "rtsp_transport", "tcp", 0); // using tcp for rtsp
	av_dict_set(&options, "stimeout", "100000", 0); // set timeout to be 100ms, in micro second
	//av_dict_set(&options, "max_delay", "500000", 0); // set max delay to be 500ms

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
	if (Debug)
	{
		av_dump_format(ifmt_Ctx, 0, CameraPath.c_str(), 0);
	}

	// Open a circular buffer
	cbuf = new CircularBuffer(30, 100 * 1000 * 1000); // 30s and 100M
	cbuf->add_stream(ifmt_Ctx->streams[0]);

	VideoRecorder* bg_recorder = new VideoRecorder();
	ret = bg_recorder->add_stream(ifmt_Ctx->streams[0]);

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
	av_usleep(1*1000*1000); // sleep for a while to have the circular buffer accumulated

	AVDictionary* dictionary = NULL;
	//av_dict_set(&dictionary, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	av_dict_set(&dictionary, "movflags", "frag_keyframe", 0);
	AVPacket pkt;
	AVRational timebase = cbuf->get_time_base();
	int64_t pts0 = 0;

	bg_recorder->set_options("movflags", "frag_keyframe");

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
				bg_recorder->close();
				fprintf(stderr, "Chunked video file saved.\n");
			}
			else
			{
				fprintf(stderr, "First background recording.\n");
			}

			// Open a new chunk
			filename_bg = prefix_videofile + std::to_string(number_bg++) + ".mp4";
			bg_recorder->open(filename_bg);
			av_dump_format(bg_recorder->get_output_format_context(), 0, filename_bg.c_str(), 1);
		}

		// read a background packet from the queue
		ret = cbuf->peek_packet(&pkt);
		if (ret > 0)
		{
			if (pts0 == 0)
			{
				pts0 = pkt.pts;
				fprintf(stderr, "The first packet: pts=%lld, pts_time=%lld \n",
					pts0, pts0 * timebase.num/timebase.den);
			}

			fprintf(stderr, "Read a packet pts time: %lld, dt: %lldms, packet size %d, total size: %d.\n",
				pkt.pts * timebase.num / timebase.den,
				1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size, ret);

			if (pkt.pts < pts0)
			{
				fprintf(stderr, "error.\n");
			}
			if (bg_recorder->record(&pkt) < 0)
			{
				fprintf(stderr, "%s muxing packet in %s.\n", 
					bg_recorder->get_error_message().c_str(), 
					filename_bg.c_str());
				break;
			}
		}
		else
		{
			av_usleep(1000 * 20); // sleep for extra 20ms when there is no more background reading
		}
		if (Debug)
		{
			fprintf(stderr, "(Debug) %s.\n", bg_recorder->get_error_message().c_str());
		}
		av_usleep(1000); // sleep for 1ms
	}
	if (ret < 0)
		fprintf(stderr, " with error %s.\n", av_err(ret));

	//// Close output
	//if (filename_bg != "" && !(ofmt_Ctx->flags & AVFMT_NOFILE))
	//	avio_closep(&ofmt_Ctx->pb);
	//avformat_free_context(ofmt_Ctx);
}

CircularBuffer::CircularBuffer()
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_TotalPkts = 0;
	m_size = 0;
	m_time_span = 0;
	m_MaxSize = 0;
	m_option = ALIGN_TO_WALL_CLOCK;
	m_wclk_offset = 0;
	m_codecpar = avcodec_parameters_alloc(); //must be allocated with avcodec_parameters_alloc() and freed with avcodec_parameters_free().
	m_pts_span = 0;
	m_stream_index = 0;
	m_time_base = AVRational{ 1, 2 };

	m_err = 0;
	m_message = "";
	
	flag_writing = false;
	flag_reading = false;
}

CircularBuffer::CircularBuffer(int time_span, int max_size)
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_codecpar = avcodec_parameters_alloc(); //must be allocated with avcodec_parameters_alloc() and freed with avcodec_parameters_free().
	m_TotalPkts = 0;
	m_size = 0;
	m_option = ALIGN_TO_WALL_CLOCK;
	m_wclk_offset = 0;
	m_pts_span = 0;
	m_time_span = time_span > 0 ? time_span : 0;
	m_MaxSize = max_size > 0 ? max_size : 0;
	m_stream_index = 0;
	m_time_base = AVRational{ 1, 2 };

	m_err = 0;
	m_message = "";

	flag_writing = false;
	flag_reading = false;
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

	avcodec_parameters_free(&m_codecpar);
}

// Add the stream into the circular buffer
//int CircularBuffer::add_stream(AVFormatContext* ifmt_ctx, int stream_index)
int CircularBuffer::add_stream(AVStream *stream, int stream_index)
{
	// check the stream
	if (!stream)
	{
		m_err = -1;
		m_message = "Empty stream is not allowed.";
		return m_err;
	}

	m_err = avcodec_parameters_copy(m_codecpar, stream->codecpar);
	if (m_err < 0)
	{
		m_message.assign(av_err(m_err));
		return m_err;
	}

	m_time_base = stream->time_base;
	m_stream_index = stream_index;
	m_pts_span = m_time_span * m_time_base.den / m_time_base.num;
	m_wclk_offset = 0; // reset the pts offset of wall clock

	// clear the circular buffer in case the stream is changed
	while (first_pkt)
	{
		AVPacketList* pktl = first_pkt;
		av_packet_unref(&pktl->pkt);
		first_pkt = pktl->next;
		av_free(pktl);
	}

	m_err = 0;
	m_message = "";
	return m_err;
};

// Set the operation option
void CircularBuffer::set_option(int option)
{
	m_option = option;
	m_err = 0;
	m_message = "";
}

// push a video or audio packet to the circular buffer, pts and dts are adjust to wall clock
// positive return indicates the packet is added successfully. The number returned is the current total packets in the circular buffer.
// 0 return indicates that the packet is added successfully but the circular buffer has suffered oversized and been revised.
// negative return indicates that the packet is not added due to run out of memory
int CircularBuffer::push_packet(AVPacket* pkt)
{
	m_err = 0;
	m_message = "";

	// empty packet is not allowed in the circular buffer
	if (!pkt)
	{
		m_err = -1;
		m_message = "packet not accepted: empty packet";
		return m_err;
	}

	// packet with different stream index is not accepted
	if (pkt->stream_index != m_stream_index)
	{
		m_err = -2;
		m_message = "packet not accepted: stream index is different";
		return m_err;
	}

	// packet that cannot be reference is not allowed
	if (av_packet_make_refcounted(pkt) < 0)
	{
		m_err = -3;
		m_message = "packet not accepted: packet cannot be referenced";
		return m_err;
	}

	// packet that has no pts is not allowed
	if (pkt->pts == AV_NOPTS_VALUE)
	{
		m_err = -4;
		m_message = "packet not accepted: packet that has no valid pts";
		return m_err;
	}

	// new a packet list, which is shall be free when getting staled
	AVPacketList* pktl = (AVPacketList*)av_mallocz(sizeof(AVPacketList));
	if (!pktl)
	{
		//av_packet_unref(pkt);
		av_free(pktl);
		m_err = -5;
		m_message = "cannot allocate new packet list";
		return m_err;
	}

	// set the writing flag to block unsafe reading
	flag_writing = true;

	// add the packet to the queue
	av_packet_ref(&pktl->pkt, pkt);  // leave the pkt alone
	//av_packet_move_ref(&pktl->pkt, pkt);  // this makes the pkt unref
	pktl->next = NULL;

	//	do the corresponding operations specified by the option 
	if (m_option & ALIGN_TO_WALL_CLOCK)
	{
		if (m_wclk_offset == 0)
		{
			// calculate the pts offset against current wall clock
			int64_t den = m_time_base.den;
			int64_t num = m_time_base.num;
			num *= 1000000L;
			int64_t gcd = av_const av_gcd(den, num);
			if (gcd)
			{
				den = den / gcd;
				num = num / gcd;
			}
			if (num > den)
				m_wclk_offset = av_gettime() * den / num - pktl->pkt.pts;
			else
				m_wclk_offset = av_gettime() / num * den - pktl->pkt.pts;
			
			//m_wclk_offset = m_wclk_offset * m_time_base.den / m_time_base.num / 1000000 - pktl->pkt.pts;
		}

		pktl->pkt.pts += m_wclk_offset;
		pktl->pkt.dts += m_wclk_offset;
	}

	// modify the pointers
	if (!last_pkt)
		first_pkt = pktl; // handle the first adding
	else
		last_pkt->next = pktl;
	last_pkt = pktl; // the new added packet is always the last packet in the circular buffer

	m_TotalPkts++;
	m_size += pktl->pkt.size + sizeof(*pktl);
	m_err = 0;

	// do not update the pointers while actively reading
	if (flag_reading)
	{
		flag_writing = false;
		m_message = "Warning: not modify the pointer for others are reading";
		return m_TotalPkts;
	}

	// update the background reader when a new packet is added
	if (!bg_pkt)
		bg_pkt = last_pkt;

	// update the main reader when a new packet is added
	if (!mn_pkt)
		mn_pkt = last_pkt;

	// revise the circular buffer by kicking out those overflowed packets
	int64_t pts_bg = bg_pkt->pkt.pts;
	int64_t pts_mn = mn_pkt->pkt.pts;
	int64_t allowed_pts = last_pkt->pkt.pts - m_pts_span;
	while ((first_pkt->pkt.pts < allowed_pts) || (m_size > m_MaxSize))
	{
		pktl = first_pkt;
		m_TotalPkts--; // update the number of total packets
		m_size -= first_pkt->pkt.size + sizeof(*first_pkt);  // update the size of the circular buffer

		av_packet_unref(&first_pkt->pkt); // unref the first packet
		first_pkt = first_pkt->next; // update the first packet list
		av_freep(&pktl);  // free the unsed packet list
	}

	// update the background reader in case it is behind the first packet
	if (pts_bg < first_pkt->pkt.pts)
		bg_pkt = first_pkt;

	// update the mn reader in case it is behind the first packet changed
	if (pts_mn < first_pkt->pkt.pts)
		mn_pkt = first_pkt;

	flag_writing = false;
	m_message = "Packet added";
	return m_TotalPkts;
}

// read a packet out of the circular buffer.
// read a packet using the background reader when isBackground is true
// read a packet using the main reader when isBackground is false
// a positive return indicates the packet is read. 
int CircularBuffer::peek_packet(AVPacket* pkt, bool isBackground)
{
	m_err = 0;
	m_message = "";

	// no reading while adding new packet
	if (flag_writing)
	{
		pkt = NULL;
		m_message = "Warning: not able to read while others are writting";
		return m_err;
	}

	flag_reading = true; // set the reading flag to stop the modify of packet list

	if (isBackground && bg_pkt)
	{
		av_packet_ref(pkt, &bg_pkt->pkt); // expose to the outside a copy of the packet
		bg_pkt = bg_pkt->next; // update the reader packet
		flag_reading = false;
		m_message = "packet read for background recording";
		return m_size; // return the number of total packets
	}

	if (!isBackground && mn_pkt)
	{
		av_packet_ref(pkt, &mn_pkt->pkt); // expose to the outside a copy of the packet
		mn_pkt = mn_pkt->next; // update the reader packet
		flag_reading = false;
		m_message = "packet read for main recording";
		return m_size;
	}

	pkt = NULL;
	flag_reading = false;
	m_message = "no packet available to read at this moment";
	return 0;
};

//// read a packet out of the circular buffer for main event recording
//// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
//// 0 return indicates no available packet is read. Or the main event reading has reached the end.
//int CircularBuffer::read_mn_packet(AVPacket* pkt)
//{
//	// no reading while adding new packet
//	if (flag_writing)
//		return 0;
//
//	flag_reading = true;
//	int ret = 0;
//	if (mn_pkt)
//	{
//		//*pkt = bg_pkt->pkt;  // this will make the pkt expose to outside
//		av_packet_ref(pkt, &mn_pkt->pkt); // expose to the outside a copy of the packet
//		mn_pkt = mn_pkt->next;
//		ret = m_TotalPkts; // return the number of total packets
//	}
//
//	flag_reading = false;
//	return ret;
//};

//// reset the main event reading pointer to s seconds from the last packet
//void CircularBuffer::reset_mn_read(int s)
//{
//	mn_pkt = first_pkt;
//	
//	// assign the main record pointer to the first packet in case s is too big
//	if (s >= m_time_span)
//	{
//		return;
//	}
//
//	// assign the main record pointer to the last packet in case s is too small
//	if (s <= 0)
//	{
//		mn_pkt = last_pkt;
//		return;
//	}
//
//	int64_t allowed_pts_video = 0;
//	int64_t allowed_pts_audio = 0;
//
//	// calculate audio pts limit in case there exists audio stream
//	if (m_index_audio >= 0)
//	{
//		allowed_pts_audio = s * m_ifmt_Ctx->streams[m_index_audio]->time_base.den / m_ifmt_Ctx->streams[m_index_audio]->time_base.num;
//	}
//
//	// calculate video pts limit in case there exists video stream
//	if (m_index_video >= 0)
//	{
//		allowed_pts_video = s * m_ifmt_Ctx->streams[m_index_video]->time_base.den / m_ifmt_Ctx->streams[m_index_video]->time_base.num;
//	}
//
//	// set the pts limits if the last packet is audio
//	if (last_pkt->pkt.stream_index == m_index_audio)
//	{
//		allowed_pts_audio = last_pkt->pkt.pts - allowed_pts_audio;  // set the pts limit of audio packets. No timebase adjustment is needed
//
//		// set the pts limit of video packets. timebase conversion is needed
//		if (m_index_video >= 0)
//		{
//			allowed_pts_video = av_rescale_q(allowed_pts_audio, m_ifmt_Ctx->streams[m_index_audio]->time_base, m_ifmt_Ctx->streams[m_index_video]->time_base);
//		}
//	}
//
//	// set the pts limits if the last packet is video
//	if (last_pkt->pkt.stream_index == m_index_video)
//	{
//		allowed_pts_video = last_pkt->pkt.pts - allowed_pts_video;  // set the pts limit of video packets. No timebase adjustment is needed
//
//		// set the pts limit of audio packets. timebase conversion is needed
//		if (m_index_audio >= 0)
//		{
//			allowed_pts_audio = av_rescale_q(allowed_pts_video, m_ifmt_Ctx->streams[m_index_video]->time_base, m_ifmt_Ctx->streams[m_index_audio]->time_base);
//		}
//	}
//
//	while ((mn_pkt->pkt.stream_index == m_index_audio && mn_pkt->pkt.pts < allowed_pts_audio)
//		|| (first_pkt->pkt.stream_index == m_index_video && first_pkt->pkt.pts < allowed_pts_video))
//	{
//		mn_pkt = mn_pkt->next;
//	}
//	return;
//};

// get the time base of the circular buffer
AVRational CircularBuffer::get_time_base()
{
	m_err = 0;
	m_message = "";

	return m_time_base;
};

// get the size of the circular buffer
int CircularBuffer::get_size()
{
	m_err = 0;
	m_message = "";

	return m_size;
};

// get the codec parameters of the circular buffer
AVCodecParameters* CircularBuffer::get_stream_codecpar()
{
	m_err = 0;
	m_message = "";

	return m_codecpar;
};

// get the error message of last operation
std::string CircularBuffer::get_error_message()
{
	return m_message;
}

// reset the main reader to the very beginning of the circular buffer
void CircularBuffer::reset_main_reader()
{
	m_err = 0;
	m_message = "";

	mn_pkt = first_pkt;
}

VideoRecorder::VideoRecorder()
{
	m_url = "";
	m_ofmt_Ctx = NULL;
	m_options = NULL;
	m_index_video = -1;
	m_index_audio = -1;
	m_flag_interleaved = true;
	m_tbf_video = AVRational{ 1,2 };
	m_tbf_audio = AVRational{ 1,3 };
	m_time_base_audio = AVRational{ 1,4 };
	m_time_base_video = AVRational{ 1,5 };
	m_pts_offset_video = 0;
	m_pts_offset_audio = 0;
	m_defalt_duration_audio = 0;
	m_defalt_duration_video = 0;
	m_err = 0;
	m_message = "";
}

VideoRecorder::~VideoRecorder()
{
	avformat_free_context(m_ofmt_Ctx);
	av_dict_free(&m_options);
}

int VideoRecorder::set_options(std::string option, std::string value)
{
	m_err = 0;
	m_message = "";

	return av_dict_set(&m_options, option.c_str(), value.c_str(), 0);
}

int VideoRecorder::add_stream(AVStream* stream)
{
	m_err = 0;
	m_message = "";

	// return an error code when no stream can be found
	if (!stream)
	{
		m_err = -1;
		m_message = "Error. Empty stream cannot be added";
		return m_err;
	}

	// Create a new format context for the output container format.
	m_err = avformat_alloc_output_context2(&m_ofmt_Ctx, NULL, "mp4", NULL);
	if (m_err < 0)
	{
		//m_message = "Error. Could not allocate output format context.";
		m_message.assign(av_err(m_err));
		return m_err;
	}

	//m_ifmt_Ctx = ifmt_Ctx;
	AVStream* out_stream = avformat_new_stream(m_ofmt_Ctx, NULL);
	if (!out_stream)
	{
		m_err = -2;
		m_message = "Error. Failed allocating output stream.";
		return m_err;
	}

	m_err = avcodec_parameters_copy(out_stream->codecpar, stream->codecpar);
	if (m_err < 0)
	{
		m_err = -3;
		//m_message = "Error. Failed to copy the stream codec parameters.";
		m_message.assign(av_err(m_err));
		return m_err;
	}

	out_stream->id = m_ofmt_Ctx->nb_streams - 1;
	out_stream->codecpar->codec_tag = 0;

	if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		m_index_video = out_stream->id;
		m_time_base_video = stream->time_base;
	}
	else if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
	{
		m_index_audio = out_stream->id;
		m_time_base_audio = stream->time_base;
	}

	m_message = "stream is added to the recorder";
	return m_err;
}

int VideoRecorder::open(std::string url)
{
	m_err = 0;
	m_message = "";

	m_url = url;
	AVDictionary* dictionary = NULL;
	//av_dict_set(&dictionary, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	av_dict_set(&dictionary, "movflags", "frag_keyframe", 0);

	// try to solve the 
	for (int i = 0; i < m_ofmt_Ctx->nb_streams; i++)
	{
		m_ofmt_Ctx->streams[i]->start_time = AV_NOPTS_VALUE;
		m_ofmt_Ctx->streams[i]->duration = AV_NOPTS_VALUE;
		m_ofmt_Ctx->streams[i]->first_dts = AV_NOPTS_VALUE;
		m_ofmt_Ctx->streams[i]->cur_dts = AV_NOPTS_VALUE;
	}
	m_ofmt_Ctx->output_ts_offset = 0;

	if (!(m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE))
	{
		m_err = avio_open(&m_ofmt_Ctx->pb, m_url.c_str(), AVIO_FLAG_WRITE);
		if (m_err < 0)
		{
			//fprintf(stderr, "Could not open output file %sn", m_url.c_str());
			m_message.assign(av_err(m_err));
			m_message = "Could not open " + url + " with error " + m_message;
			return m_err;
		}
	}
	else
	{
		m_err = -1;
		m_message = "Error: A file has already been associates to the context.";
		return m_err;
	}

	m_err = avformat_write_header(m_ofmt_Ctx, &dictionary);
	if (m_err < 0)
	{
		m_message.assign(av_err(m_err));
		m_message += " Could not open " + url;
		return m_err;
	}

	m_message = url + " is openned with return code " + std::to_string(m_err);
	m_err = 0;

	// update the time base factors used to rescale the time stamps of input packets to the output stream
	m_tbf_audio = m_time_base_audio;
	m_tbf_video = m_time_base_video;
	m_defalt_duration_audio = 440;
	m_defalt_duration_video = 3000;
	
	if (m_index_audio >= 0)
	{
		m_tbf_audio.den *= m_ofmt_Ctx->streams[m_index_audio]->time_base.num;
		m_tbf_audio.num *= m_ofmt_Ctx->streams[m_index_audio]->time_base.den;
		m_defalt_duration_audio = m_ofmt_Ctx->streams[m_index_audio]->time_base.den / m_ofmt_Ctx->streams[m_index_audio]->time_base.num / 44000;
	}

	if (m_index_video >= 0)
	{
		m_tbf_video.den *= m_ofmt_Ctx->streams[m_index_video]->time_base.num;
		m_tbf_video.num *= m_ofmt_Ctx->streams[m_index_video]->time_base.den;
		m_defalt_duration_video = m_ofmt_Ctx->streams[m_index_video]->time_base.den / m_ofmt_Ctx->streams[m_index_video]->time_base.num / 30;
	}
	m_pts_offset_audio = 0;
	m_pts_offset_video = 0;

	return m_err;
}

int VideoRecorder::record(AVPacket* pkt, int stream_index)
{
	m_err = 0;
	m_message = "";

	// rescale the time stamp to the output stream
	if (stream_index == m_index_audio)
	{
		pkt->pts *= m_tbf_audio.num / m_tbf_audio.den;
		pkt->dts *= m_tbf_audio.num / m_tbf_audio.den;

		if (pkt->duration)
		{
			pkt->duration *= m_tbf_audio.num / m_tbf_audio.den;
		}
		else
		{
			pkt->duration = m_defalt_duration_audio;
		}

		if (m_pts_offset_audio == 0)
		{
			m_pts_offset_audio = -pkt->pts;
		}
		pkt->pts += m_pts_offset_audio;
		pkt->dts += m_pts_offset_audio;
	}
	else if (stream_index == m_index_video)
	{
		pkt->pts *= m_tbf_video.num / m_tbf_video.den;
		pkt->dts *= m_tbf_video.num / m_tbf_video.den;
		if (pkt->duration)
		{
			pkt->duration *= m_tbf_video.num / m_tbf_video.den;
		}
		else
		{
			pkt->duration = m_defalt_duration_video;
		}

		if (m_pts_offset_audio == 0)
		{
			m_pts_offset_audio = -pkt->pts;
		}
		pkt->pts += m_pts_offset_audio;
		pkt->dts += m_pts_offset_audio;
	}

	pkt->stream_index = stream_index;
	pkt->pos = -1;
	
	// check the interleaved flag
	if (m_flag_interleaved)
	{
		m_err = av_interleaved_write_frame(m_ofmt_Ctx, pkt); // interleaved write will handle the packet unref
	}
	else
	{
		m_err = av_write_frame(m_ofmt_Ctx, pkt);
		av_packet_unref(pkt);
	}

	m_message = "packet written";
	if (m_err)
	{
		//m_message = "Error. Cannot write the packet with error code " + std::to_string(m_err);
		m_message.assign(av_err(m_err));
	}
	return m_err;
}

int VideoRecorder::close()
{
	// no close when no file is opened
	if (m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE)
		return -1;

	m_err = av_write_trailer(m_ofmt_Ctx);
	if (m_err < 0)
	{
		m_message.assign(av_err(m_err));
		return m_err;
	}

	avio_closep(&m_ofmt_Ctx->pb);

	m_message = m_url + " is closed.";
	return m_err;
}

// get the stream codec parameter
AVFormatContext* VideoRecorder::get_output_format_context()
{
	m_err = 0;
	m_message = "";

	return m_ofmt_Ctx;
};

// get the stream time base
AVRational VideoRecorder::get_stream_time_base(int stream_index)
{
	m_err = 0;
	m_message = "";

	if (stream_index >= 0 && stream_index < m_ofmt_Ctx->nb_streams)
	{
		return m_ofmt_Ctx->streams[stream_index]->time_base;
	}

	return AVRational{ 1,1 };
};

// get the error message of last operation
std::string VideoRecorder::get_error_message()
{
	return m_message;
}

