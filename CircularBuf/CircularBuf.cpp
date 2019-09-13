#include <ctime>
#include <iostream>
#include <string>
#include <string.h>
#include <stdio.h>
#include <Windows.h>
//#include <pthread.h>

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
	CircularBuffer(int time_span, int max_size, AVFormatContext* ifmt_Ctx);
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

	// reset the main event reading pointer to s seconds from the last packet
	void reset_mn_read(int s);

	// get the input format context
	AVFormatContext* get_input_format_context();

protected:
	AVPacketList* first_pkt; // pointer to the first added packet in the circular buffer
	AVPacketList* last_pkt; // pointer to the new added packet in the circular buffer
	AVPacketList* bg_pkt; // the background reading pointer
	AVPacketList* mn_pkt;// the background reading pointer
	AVFormatContext* m_ifmt_Ctx; // The camera input format context

	int m_TotalPkts; // counter of total packets in the circular buffer
	int m_size;  // total size of the packets in the buffer
	int64_t m_time_span;  // max time span in seconds
	int64_t m_pts_offset_video; // offset of video stream pts against the epoch wall clock in miliseconds
	int64_t m_pts_offset_audio; // offset of audio stream pts against the epoch wall clock in miliseconds
	int m_index_video;
	int m_index_audio;
	int m_MaxSize;

	bool flag_writing; // flag indicates adding new packet to the circular buffer
	bool flag_reading; // flag indicates reading from the circular buffer
};

// global variables
CircularBuffer* cbuf; // global shared the circular buffer
AVFormatContext* ifmt_Ctx = NULL;  // global shared input format context
int64_t lastReadPacktTime = 0; // global shared time stamp for call back
std::string prefix_videofile = "C:\\Users\\georges\\Documents\\CopTraxTemp\\";

class VideoRecorder
{
public:
	VideoRecorder();
	~VideoRecorder();

	int init(AVCodecParameters* instream_codecpar);
	int open(std::string url);  // open a video recorder specified by url, can be a filename or a rtp url
	int close();  // close the video recorder
	int record(AVPacket* pkt); // save the packet to the video recorder
	int set_options(std::string option, std::string value); // set the options for video recorder, has to be called before open

protected:
	std::string m_url;
	AVFormatContext* m_ofmt_Ctx;
	AVDictionary* m_options;
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

		// save those video stream only
		if (pkt.pts == AV_NOPTS_VALUE || pkt.stream_index != index_video)
		{
			av_packet_unref(&pkt);
			continue;
		}

		ret = cbuf->add_packet(&pkt);  // add the packet to the circular buffer
		if (ret > 0)
			fprintf(stderr, "Add a new packet (%lld, %d) into the circular buffer of %d packets now.\n", 
				1000 * pkt.pts * ifmt_Ctx->streams[index_video]->time_base.num / ifmt_Ctx->streams[index_video]->time_base.den, pkt.size, ret);
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
	//std::string CameraPath = "rtsp://10.25.50.20/h264";
	std::string CameraPath = "rtsp://10.0.9.113:8554/0";
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
	av_dump_format(ifmt_Ctx, 0, CameraPath.c_str(), 0);

	// Open a circular buffer
	cbuf = new CircularBuffer(30, 100 * 1000 * 1000, ifmt_Ctx); // 30s and 100M

	// assign the output format. It is used for background recordings
	AVFormatContext* ofmt_Ctx = NULL;
	avformat_alloc_output_context2(&ofmt_Ctx, NULL, "mp4", NULL);
	if (!ofmt_Ctx)
	{
		fprintf(stderr, "Could not create output context.");
		exit(1);
	}
	AVOutputFormat* ofmt = ofmt_Ctx->oformat;

	// find the index of video stream
	int index_video = -1;
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

	// Add a new stream to the recording file. It is required to be called before avformat_write_header()
	AVStream* out_stream = avformat_new_stream(ofmt_Ctx, NULL);
	if (!out_stream)
	{
		fprintf(stderr, "Failed allocating output stream.");
		exit(1);
	}

	// copy the IP camera stream parameters to the video file stream
	ret = avcodec_parameters_copy(out_stream->codecpar, ifmt_Ctx->streams[index_video]->codecpar);
	if (ret < 0)
	{
		fprintf(stderr, "Failed to copy codec parameters.");
		exit(1);
	}

	out_stream->id = ofmt_Ctx->nb_streams - 1;
	out_stream->codecpar->codec_tag = 0;
	//out_stream->time_base.num = 1;
	//out_stream->time_base.den = 1000; // wall clock time base is 1/1000
	//out_stream->avg_frame_rate.num = 30 * out_stream->time_base.den;
	//out_stream->avg_frame_rate.den = out_stream->time_base.den;
	//out_stream->r_frame_rate.num = 30 * out_stream->time_base.den;
	//out_stream->r_frame_rate.den = out_stream->time_base.den;
	//out_stream->pts_wrap_bits = 64;
	//out_stream->internal->avctx->pkt_timebase = out_stream->time_base;
	//avpriv_set_pts_info(out_stream, 64, 1, 1000);
	av_dump_format(ofmt_Ctx, 0, "sample.mp4", 1);
	
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
	av_usleep(5*1000*1000); // sleep for a while to have the circular buffer accumulated

	AVDictionary* dictionary = NULL;
	//av_dict_set(&dictionary, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	av_dict_set(&dictionary, "movflags", "frag_keyframe", 0);
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
			filename_bg = prefix_videofile + std::to_string(number_bg++) + ".mp4";
			av_dump_format(ofmt_Ctx, 0, filename_bg.c_str(), 1);

			ret = avio_open(&ofmt_Ctx->pb, filename_bg.c_str(), AVIO_FLAG_WRITE);
			if (ret < 0)
			{
				fprintf(stderr, "Could not open output file %sn", filename_bg.c_str());
				break;
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
		if (ret > 0 && pkt.stream_index == index_video)
		{
			if (pts0 == 0)
				pts0 = pkt.pts;

			pkt.pts = av_rescale_q(pkt.pts, ifmt_Ctx->streams[index_video]->time_base, ofmt_Ctx->streams[0]->time_base);
			pkt.dts = pkt.pts;
			if (pkt.duration > 0)
				pkt.duration = av_rescale_q(pkt.duration, ifmt_Ctx->streams[index_video]->time_base, ofmt_Ctx->streams[0]->time_base);
			fprintf(stderr, "Read a packet (%lld, %d), %d packets left in circular buffer.\n",
				1000 * (pkt.pts - pts0) * ofmt_Ctx->streams[0]->time_base.num / ofmt_Ctx->streams[0]->time_base.den, pkt.size, ret);

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
	m_time_span = 0;
	m_pts_offset_audio = 0;
	m_pts_offset_video = 0;
	m_MaxSize = 0;
	m_index_audio = -1;
	m_index_video = -1;
	m_ifmt_Ctx = NULL;
	
	flag_writing = false;
	flag_reading = false;
}

CircularBuffer::CircularBuffer(int time_span, int max_size, AVFormatContext *ifmt_Ctx)
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_TotalPkts = 0;
	m_size = 0;
	m_time_span = time_span;
	m_pts_offset_audio = 0;
	m_pts_offset_video = 0;
	m_MaxSize = 0;
	m_index_audio = -1;
	m_index_video = -1;

	flag_writing = false;
	flag_reading = false;
	m_ifmt_Ctx = ifmt_Ctx;

	for (int i = 0; i < m_ifmt_Ctx->nb_streams; i++)
	{
		if (m_ifmt_Ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			m_index_video = i;
		}
		else if (m_ifmt_Ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			m_index_audio = i;
		}
	}

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

// add a video or audio packet to the circular buffer, pts and dts are adjust to wall clock
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
	int64_t allowed_pts_video = 0;
	int64_t allowed_pts_audio = 0;
	if (m_index_video >= 0)
		allowed_pts_video = m_time_span * m_ifmt_Ctx->streams[m_index_video]->time_base.den / m_ifmt_Ctx->streams[m_index_video]->time_base.num;  // max video pts
	if (m_index_audio >= 0)
		allowed_pts_audio = m_time_span * m_ifmt_Ctx->streams[m_index_audio]->time_base.den / m_ifmt_Ctx->streams[m_index_audio]->time_base.num;  // max audio pts

	// adjust the packet pts to the wall clock based
	if (pkt->stream_index == m_index_audio)
	{
		if (m_pts_offset_audio ==0)
			m_pts_offset_audio = av_gettime() / m_ifmt_Ctx->streams[m_index_audio]->time_base.num * m_ifmt_Ctx->streams[m_index_audio]->time_base.den / 1000000 - pkt->pts;
		pktl->pkt.pts += m_pts_offset_audio;
		pktl->pkt.dts += m_pts_offset_audio;
		allowed_pts_audio = pktl->pkt.pts - allowed_pts_audio;
		if (m_index_video >= 0)
			allowed_pts_video = av_rescale_q(pktl->pkt.pts, m_ifmt_Ctx->streams[m_index_audio]->time_base, m_ifmt_Ctx->streams[m_index_video]->time_base)
			- allowed_pts_video;
	}
	
	if (pkt->stream_index == m_index_video)
	{
		if (m_pts_offset_video == 0)
			m_pts_offset_video = av_gettime() / m_ifmt_Ctx->streams[m_index_video]->time_base.num * m_ifmt_Ctx->streams[m_index_video]->time_base.num / 1000000 - pkt->pts;
		pktl->pkt.pts += m_pts_offset_video;
		pktl->pkt.dts += m_pts_offset_video;
		allowed_pts_video = pktl->pkt.pts - allowed_pts_video;
		if (m_index_audio >= 0)
			allowed_pts_audio = av_rescale_q(pktl->pkt.pts, m_ifmt_Ctx->streams[m_index_video]->time_base, m_ifmt_Ctx->streams[m_index_audio]->time_base) 
			- allowed_pts_audio;
	}

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
	int64_t pts_bg = bg_pkt->pkt.pts;
	int64_t pts_mn = mn_pkt->pkt.pts;
	while ((first_pkt->pkt.stream_index == m_index_audio && first_pkt->pkt.pts < allowed_pts_audio) 
		|| (first_pkt->pkt.stream_index == m_index_video && first_pkt->pkt.pts < allowed_pts_video))
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
	// no reading while adding new packet
	if (flag_writing)
		return 0;

	flag_reading = true;
	int ret = 0;
	if (mn_pkt)
	{
		//*pkt = bg_pkt->pkt;  // this will make the pkt expose to outside
		av_packet_ref(pkt, &mn_pkt->pkt); // expose to the outside a copy of the packet
		mn_pkt = mn_pkt->next;
		ret = m_TotalPkts; // return the number of total packets
	}

	flag_reading = false;
	return ret;
};

// reset the main event reading pointer to s seconds from the last packet
void CircularBuffer::reset_mn_read(int s)
{
	mn_pkt = first_pkt;
	
	// assign the main record pointer to the first packet in case s is too big
	if (s >= m_time_span)
	{
		return;
	}

	// assign the main record pointer to the last packet in case s is too small
	if (s <= 0)
	{
		mn_pkt = last_pkt;
		return;
	}

	int64_t allowed_pts_video = 0;
	int64_t allowed_pts_audio = 0;

	// calculate audio pts limit in case there exists audio stream
	if (m_index_audio >= 0)
	{
		allowed_pts_audio = s * m_ifmt_Ctx->streams[m_index_audio]->time_base.den / m_ifmt_Ctx->streams[m_index_audio]->time_base.num;
	}

	// calculate video pts limit in case there exists video stream
	if (m_index_video >= 0)
	{
		allowed_pts_video = s * m_ifmt_Ctx->streams[m_index_video]->time_base.den / m_ifmt_Ctx->streams[m_index_video]->time_base.num;
	}

	// set the pts limits if the last packet is audio
	if (last_pkt->pkt.stream_index == m_index_audio)
	{
		allowed_pts_audio = last_pkt->pkt.pts - allowed_pts_audio;  // set the pts limit of audio packets. No timebase adjustment is needed

		// set the pts limit of video packets. timebase conversion is needed
		if (m_index_video >= 0)
		{
			allowed_pts_video = av_rescale_q(allowed_pts_audio, m_ifmt_Ctx->streams[m_index_audio]->time_base, m_ifmt_Ctx->streams[m_index_video]->time_base);
		}
	}

	// set the pts limits if the last packet is video
	if (last_pkt->pkt.stream_index == m_index_video)
	{
		allowed_pts_video = last_pkt->pkt.pts - allowed_pts_video;  // set the pts limit of video packets. No timebase adjustment is needed

		// set the pts limit of audio packets. timebase conversion is needed
		if (m_index_audio >= 0)
		{
			allowed_pts_audio = av_rescale_q(allowed_pts_video, m_ifmt_Ctx->streams[m_index_video]->time_base, m_ifmt_Ctx->streams[m_index_audio]->time_base);
		}
	}

	while ((mn_pkt->pkt.stream_index == m_index_audio && mn_pkt->pkt.pts < allowed_pts_audio)
		|| (first_pkt->pkt.stream_index == m_index_video && first_pkt->pkt.pts < allowed_pts_video))
	{
		mn_pkt = mn_pkt->next;
	}
	return;
};

// get the input format context in circular buffer
AVFormatContext* CircularBuffer::get_input_format_context()
{
	return m_ifmt_Ctx;
};


VideoRecorder::VideoRecorder()
{
	m_url = "";
	m_ofmt_Ctx = NULL;
	m_options = NULL;
}

VideoRecorder::~VideoRecorder()
{
	avformat_free_context(m_ofmt_Ctx);
	av_dict_free(&m_options);
}

int VideoRecorder::set_options(std::string option, std::string value)
{
	return av_dict_set(&m_options, option.c_str(), value.c_str(), 0);
}

int VideoRecorder::init(AVCodecParameters* instream_codecpar)
{
	// Create a new format context for the output container format.
	int ret = avformat_alloc_output_context2(&m_ofmt_Ctx, NULL, "mp4", NULL);
	if (ret < 0)
	{
		fprintf(stderr, "Could not allocate output format context\n");
		return ret;
	}

	AVStream* out_stream = avformat_new_stream(m_ofmt_Ctx, NULL);
	if (!out_stream)
	{
		fprintf(stderr, "Failed allocating output stream\n");
		return AVERROR_UNKNOWN;
	}

	ret = avcodec_parameters_copy(out_stream->codecpar, instream_codecpar);
	if (ret < 0)
	{
		fprintf(stderr, "Failed allocating output stream.\n");
		return ret;
	}

	out_stream->id = m_ofmt_Ctx->nb_streams - 1;
	out_stream->codecpar->codec_tag = 0;
	out_stream->time_base.num = 1;
	out_stream->time_base.den = 1000; // wall clock time base is 1/1000
	out_stream->avg_frame_rate.num = 30;
	out_stream->avg_frame_rate.den = 1;
	out_stream->r_frame_rate.num = 30;
	out_stream->r_frame_rate.den = 1;
	out_stream->pts_wrap_bits = 64;
}

int VideoRecorder::open(std::string url)
{
	m_url = url;
	AVDictionary* dictionary = NULL;
	//av_dict_set(&dictionary, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	av_dict_set(&dictionary, "movflags", "frag_keyframe", 0);

	int ret = 0;

	if (!(m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&m_ofmt_Ctx->pb, m_url.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			fprintf(stderr, "Could not open output file %sn", m_url.c_str());
			return ret;
		}
	}
	else
	{
		fprintf(stderr, "There is a file associate to the context already.\n");
		return ret;
	}

	ret = avformat_write_header(m_ofmt_Ctx, &dictionary);
	if (ret < 0)
		fprintf(stderr, "Could not open %s\n", url.c_str());
	return ret;
}

int VideoRecorder::record(AVPacket* pkt)
{
	return av_interleaved_write_frame(m_ofmt_Ctx, pkt);
}

int VideoRecorder::close()
{
	int ret = av_write_trailer(m_ofmt_Ctx);

	if (ret < 0)
	{
		fprintf(stderr, "Cannot write the tailer to the file.\n");
		return ret;
	}

	if (!m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE)
		avio_closep(&m_ofmt_Ctx->pb);

	return ret;
}
