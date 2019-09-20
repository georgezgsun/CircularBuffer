#include <time.h>
#include <ctime>
#include <iostream>
#include <string>
#include <string.h>
#include <stdio.h>
#include <Windows.h>
//#include <pthread.h>

#define ALIGN_TO_WALL_CLOCK 1
#define CHANGE_STREAM_INDEX 2

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
#include <libavdevice/avdevice.h>
}

class CircularBuffer
{
public:
	CircularBuffer();
	~CircularBuffer();

	// Add the stream into the circular buffer
	//int add_stream(AVFormatContext* ifmt_Ctx, int stream_index = 0);
	void open(int time_span, int max_size, int option = ALIGN_TO_WALL_CLOCK | CHANGE_STREAM_INDEX);
	int add_stream(AVStream * stream);

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

	// get the stream codec parameters that defines the packet in the circular buffer
	AVCodecParameters* get_stream_codecpar();

	// get the stream associated to the circular buffer
	AVStream* get_stream();

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
	AVStream* m_st; // The assigned stream

	int m_TotalPkts; // counter of total packets in the circular buffer
	int m_size;  // total size of the packets in the buffer
	int m_time_span;  // max time span in seconds
	int64_t m_pts_span; // pts span
	int64_t m_wclk_offset; // the pts offset to the wall clock
	int64_t m_last_pts;  // last valid pts
	AVRational m_time_base; // the time base of the bind stream
	int m_stream_index; // the desired stream index
	int m_MaxSize; // the maximum size allowed for the circular buffer 
	int m_option; // operation options

	int m_err; // the error code of last operation
	std::string m_message; // the error message of last operation

	bool flag_writing; // flag indicates adding new packet to the circular buffer
	bool flag_reading; // flag indicates reading from the circular buffer
};

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
	// chunk > 0 indicates save to a series of chunked file in interval chunk seconds, url is used as prefix
	int open(std::string url, int chunk = 0);

	// make another chunked recording
	// first stop current recording in case there is one. Then start another recording by chunk prefix.
	// return 0 on success
	int chunk();

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

	// get the recording filename or url
	std::string get_url();

protected:
	std::string m_url;
	AVFormatContext* m_ofmt_Ctx;
	AVDictionary* m_options;
	AVStream* m_video_stream;
	AVStream* m_audio_stream;
	int m_index_video;
	int m_index_audio;
	int m_chunk_interval;
	int64_t m_chunk_time;
	bool m_flag_interleaved;
	bool m_flag_wclk;

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
	std::string m_chunk_prefix;
	std::string m_format;
};

class Camera
{
public:
	Camera();
	~Camera();

	// set the options for video recorder, has to be called before open
	int set_options(std::string option, std::string value);

	// open the camera for reading
	int open(std::string url = "");

	// read a packet from the camera
	int read_packet(AVPacket* pkt);

	// get the input format context
	AVFormatContext* get_input_format_context();

	// get the stream time base
	AVRational get_stream_time_base(int stream_index = 0);

	// get the specified stream
	// index can be video index or audio index
	AVStream* get_stream(int stream_index = 0);

	// get the video stream index of the camera
	// negative return indicates no video stream in the camera
	int get_video_index();

	// get the audio stream index of the camera
	// negative return indicates no audio stream in the camera
	int get_audio_index();

	// get the error message of last operation
	std::string get_error_message();

protected:
	std::string m_url;
	AVFormatContext* m_ifmt_Ctx;
	AVDictionary* m_options;
	int64_t m_start_time;  // hold the start time when the camera was opened
	int64_t m_pts_offset_audio;  // hold the audio pts offset, for wall clock alignment
	int64_t m_pts_offset_video;  // hold the video pts offset, for wall clock alignment
	int m_index_video;
	int m_index_audio;
	bool m_wclk_align;

	int m_err; // the error code of last operation
	std::string m_message; // the error message of last operation
	std::string m_format; // the camera format, can be rtsp, rtp, v4l2, dshow, file
};

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

const std::string get_date_time()
{
	time_t t = std::time(0); // get current time
	char buf[50];
	tm now;
	localtime_s(&now, &t);
	strftime(buf, sizeof(buf), "%F-%H%M%S", &now);

	return buf;
}

// global variables
CircularBuffer* cbuf; // global shared the circular buffer
Camera* ipCam; // global shared IP camera
//AVFormatContext* ifmt_Ctx = NULL;  // global shared input format context
int64_t lastReadPacktTime = 0; // global shared time stamp for call back
std::string prefix_videofile = "C:\\Users\\georges\\Documents\\CopTraxTemp\\";
int Debug = 2;

Camera::Camera()
{
	m_url = "";
	m_ifmt_Ctx = NULL;
	m_options = NULL;
	m_index_video = -1;
	m_index_audio = -1;

	m_message = "";
	m_err = avformat_network_init();
	avdevice_register_all();
	m_ifmt_Ctx = avformat_alloc_context();
	m_format = "rtsp";
	m_start_time = 0;
	m_pts_offset_video = 0;
	m_pts_offset_audio = 0;
	m_wclk_align = true;
}

Camera::~Camera()
{
	avformat_free_context(m_ifmt_Ctx);
}

// get the error message of last operation
std::string Camera::get_error_message()
{
	return m_message;
}

AVFormatContext* Camera::get_input_format_context()
{
	m_err = 0;
	m_message = "";

	return m_ifmt_Ctx;
}

// get the stream time base
AVRational Camera::get_stream_time_base(int stream_index)
{
	m_err = 0;
	m_message = "";

	if (stream_index >= 0 && stream_index < m_ifmt_Ctx->nb_streams)
	{
		return m_ifmt_Ctx->streams[stream_index]->time_base;
	}

	m_err = -1;
	m_message = "Error. No input format context is found.";
	return AVRational{ 1,1 };
};

// Set the options that specify the open of a camera
int Camera::set_options(std::string option, std::string value)
{
	m_err = 0;
	m_message = "";

	if (option == "format")
	{
		m_format = value;
		m_message = "update the camera format to be " + value;
	}
	else if (option == "wall_clock")
	{
		m_wclk_align = value == "true";
	}
	else
	{
		m_err = av_dict_set(&m_options, option.c_str(), value.c_str(), 0);
		m_message = "set the option '" + option + "' to be " + value;
	}
	return m_err;
}

// open/connect to the camera
// return 0 on success
int Camera::open(std::string url)
{
	// check if the url is empty
	if (url.empty())
	{
		m_err = -1;
		m_message = "Error. Camera path is empty";
		return m_err;
	}
	m_url = url;

	// to determine the camera type
	m_message = "IP Camera: ";
	AVInputFormat* ifmt = NULL;
	if (url.find("/dev/") != std::string::npos)
	{
		ifmt = av_find_input_format("v4l2");
		m_message = "v4l2: ";
	}
	else
	{
		ifmt = av_find_input_format(m_format.c_str());
		m_message = m_format + ": "; // "USB (Windows)";
	}

	m_err = avformat_open_input(&m_ifmt_Ctx, m_url.c_str(), ifmt, &m_options);
	if (m_err < 0)
	{
		m_message.append(av_err(m_err));
		return m_err;
	}
	m_start_time = av_gettime();  // hold global start time in microseconds

	m_err = avformat_find_stream_info(m_ifmt_Ctx, 0);
	if (m_err < 0)
	{
		m_message.append(av_err(m_err));
		return m_err;
	}

	m_err = 0;
	m_message += " connected";

	// find the index of video stream
	AVStream* st;
	for (int i = 0; i < m_ifmt_Ctx->nb_streams; i++)
	{
		st = m_ifmt_Ctx->streams[i];
		int64_t den = st->time_base.den;
		int64_t num = st->time_base.num;
		num *= 1000000;
		int64_t gcd = av_const av_gcd(den, num);
		if (gcd)
		{
			den = den / gcd;
			num = num / gcd;
		}

		int64_t pts_offset;
		if (num > den)
		{
			pts_offset = m_start_time * den / num - st->start_time;
		}
		else
		{
			pts_offset = m_start_time / num * den - st->start_time;
		}

		if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			m_index_video = i;
			m_pts_offset_video = pts_offset;
			m_message += ", video stream found";
		}

		if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			m_index_audio = i;
			m_pts_offset_audio = pts_offset;
			m_message += ", audio stream found";
		}
	}

	if (m_index_video < 0 && m_index_audio < 0)
	{
		m_err = -2;
		m_message += ", but no video nor audio stream";
	}
	return m_err;
}

// read a packet from the camera
// return 0 on success
int Camera::read_packet(AVPacket* pkt)
{
	m_message = "";
	m_err = av_read_frame(m_ifmt_Ctx, pkt); // read a frame from the camera

	// handle the timeout
	if (m_err < 0)
	{
		m_message.assign(av_err(m_err));
		m_message = "Time out while reading " + m_url + " with error " + m_message;
		pkt = NULL;
		return m_err;
	}

	if (!m_wclk_align)
	{
		return m_err;
	}

	int64_t pts_offset = pkt->stream_index == m_index_audio ? m_pts_offset_audio : m_pts_offset_video;

	// Doing wall clock alignment
	AVStream* st = m_ifmt_Ctx->streams[pkt->stream_index];
	if (pkt->pts == AV_NOPTS_VALUE)
	{
		m_err = -1;
		m_message = "pts has no value";
		return m_err;
	}
	else
	{
		pkt->pts += pts_offset;
		pkt->dts += pts_offset;
	}

	return m_err;
};

// get the video stream index of the camera
// negative return indicates no video stream in the camera
int Camera::get_video_index()
{
	return m_index_video;
}

// get the audio stream index of the camera
// negative return indicates no audio stream in the camera
int Camera::get_audio_index()
{
	return m_index_audio;
}

// get the specified stream
// index can be video index or audio index
AVStream* Camera::get_stream(int stream_index)
{
	if (stream_index < 0 || stream_index >= m_ifmt_Ctx->nb_streams)
	{
		m_err = -1;
		m_message = "In valid stream index specified";
		return NULL;
	}
	
	m_err = 0;
	m_message = "get the stream";
	return m_ifmt_Ctx->streams[stream_index];
}

static int interrupt_cb(void* ctx)
{
	int64_t  timeout = 100 * 1000 * 1000;
	if (av_gettime() - lastReadPacktTime > timeout)
	{
		return -1;
	}
	return 0;
};

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
	m_st = (AVStream *)av_mallocz(sizeof(AVStream));
	m_pts_span = 0;
	m_stream_index = 0;
	m_time_base = AVRational{ 1, 2 };

	m_err = 0;
	m_message = "";
	m_last_pts = 0;
	
	flag_writing = false;
	flag_reading = false;

	m_codecpar = avcodec_parameters_alloc(); //must be allocated with avcodec_parameters_alloc() and freed with avcodec_parameters_free().
}

void CircularBuffer::open(int time_span, int max_size, int option)
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	//
	m_TotalPkts = 0;
	m_size = 0;
	m_option = option;
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
int CircularBuffer::add_stream(AVStream *stream)
{
	// check the stream
	if (!stream)
	{
		m_err = -1;
		m_message = "Empty stream is not allowed.";
		return m_err;
	}

	// copy the codec parameters to local
	m_err = avcodec_parameters_copy(m_codecpar, stream->codecpar);
	if (m_err < 0)
	{
		m_message.assign(av_err(m_err));
		return m_err;
	}

	// copy a couple of important parameters to local stream
	m_st->codecpar = m_codecpar; // store the same codec parameters in the local stream
	m_st->time_base = stream->time_base;
	m_st->start_time = stream->start_time;
	m_st->r_frame_rate = stream->r_frame_rate;
	m_st->avg_frame_rate = stream->avg_frame_rate;
	m_st->sample_aspect_ratio = stream->sample_aspect_ratio;

	m_time_base = stream->time_base;
	m_stream_index = stream->index;
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
		m_message = "packet unacceptable: empty";
		return m_err;
	}

	// packet with different stream index is not accepted
	if (pkt->stream_index != m_stream_index)
	{
		m_err = -2;
		m_message = "packet punacceptable: stream index is different";
		return m_err;
	}

	// packet that cannot be reference is not allowed
	if (av_packet_make_refcounted(pkt) < 0)
	{
		m_err = -3;
		m_message = "packet unacceptable: cannot be referenced";
		return m_err;
	}

	// packet that has no pts is not allowed
	if (pkt->pts == AV_NOPTS_VALUE)
	{
		m_err = -4;
		m_message = "packet unacceptable: has no valid pts";
		return m_err;
	}

	// new a packet list, which is going to be freed when getting staled later
	AVPacketList* pktl = (AVPacketList*)av_mallocz(sizeof(AVPacketList));
	if (!pktl)
	{
		//av_packet_unref(pkt);
		av_free(pktl);
		m_err = -5;
		m_message = "cannot allocate new packet list";
		return m_err;
	}

	// packet that is non monotonically increasing
	if (m_last_pts == 0)
	{
		m_last_pts = pkt->pts;
	}

	if (pkt->pts < m_last_pts)
	{
		m_err = -6;
		m_message = "packet unacceptable: non monotonically increasing";
	}

	// add the packet to the queue
	av_packet_ref(&pktl->pkt, pkt);  // leave the pkt alone
	//av_packet_move_ref(&pktl->pkt, pkt);  // this makes the pkt unref
	pktl->next = NULL;

	if (m_option & CHANGE_STREAM_INDEX)
	{
		pktl->pkt.stream_index = 0;
	}

	// set the writing flag to block unsafe reading
	flag_writing = true;

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
		flag_writing = false; // clear the writing flag
		m_message = "Warning: not modify the pointer for others are reading";
		return m_TotalPkts;
	}

	// update the background reader when a new packet is added
	if (!bg_pkt)
	{
		bg_pkt = last_pkt;
	}

	// update the main reader when a new packet is added
	if (!mn_pkt)
	{
		mn_pkt = last_pkt;
	}

	// maintain the circular buffer by kicking out those overflowed packets
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
	{
		bg_pkt = first_pkt;
	}

	// update the mn reader in case it is behind the first packet changed
	if (pts_mn < first_pkt->pkt.pts)
	{
		mn_pkt = first_pkt;
	}

	flag_writing = false; // clear the writting flag
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

// get the stream assigned to the circular buffer
AVStream* CircularBuffer::get_stream()
{
	return m_st;
}

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
	m_flag_wclk = true;
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
	m_chunk_time = 0; // chunk indicator
	m_chunk_interval = 0;
	m_chunk_prefix = "";
	m_format = "mp4";
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

	if (option == "wall_clock")
	{
		if (value == "false")
		{
			m_flag_wclk = false;

			m_err = 0;
			m_message = "'wall clock alignment' flag is set to false";
		}
		else if (value == "true")
		{ 
			m_flag_wclk = true;

			m_err = 0;
			m_message = "'wall clock alignment' flag is set to true";
		}
		else
		{
			m_message = "unkown value of '" + value + "' for 'wall clock alignment' flag setting.";
			m_err = -1;
		}
		return m_err;
	}

	if (option == "interleaved_write")
	{
		if (value == "false")
		{
			m_flag_interleaved = false;

			m_err = 0;
			m_message = "'interleaved writting' flag is set to false";
		}
		else if (value == "true")
		{
			m_flag_interleaved = true;

			m_err = 0;
			m_message = "'interleaved writting' flag is set to true";
		}
		else
		{
			m_message = "unkown value of '" + value + "' for 'interleaved writting' flag setting.";
			m_err = -1;
		}
		return m_err;
	}

	if (option == "format")
	{
		if (value.length() < 1 || value.length() > 10)
		{
			m_format = "mp4";

			m_err = -1;
			m_message = value + " is invalid for 'file format' option setting";
		}
		else
		{
			m_format = value;
			m_message = "'file format' option is set to be " + value;
			m_err = 0;
		}

		return m_err;
	}
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
		m_video_stream = out_stream;
	}
	else if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
	{
		m_index_audio = out_stream->id;
		m_time_base_audio = stream->time_base;
		m_audio_stream = out_stream;
	}

	m_message = "stream is added to the recorder";
	return m_err;
}

int VideoRecorder::open(std::string url, int chunk_interval)
{
	if (url.empty())
	{
		m_err = -1;
		m_message = "url cannot be empty";
		return m_err;
	}

	// validate the input chunk interval
	if (chunk_interval < 0 || chunk_interval > 3600)
	{
		m_err = -1;
		m_message = "'chunk interval' setting shall be [0-3600]";
		return m_err;
	}

	// positive chunk interval indicates segmentally recording, url is considered to be the file prefix of segmental files
	if (chunk_interval > 0)
	{
		m_chunk_prefix = url;
		m_chunk_interval = chunk_interval * 1000;  // convert the interval into miliseconds
		m_message = "chunked recording is set to be " + m_chunk_prefix + "yyyy-MM-dd-hhmmss" + m_chunk_prefix;
	}
	else
	{
		m_url = url;  // url is the full path of the recording video file
		m_chunk_interval = 0; 
		m_chunk_prefix = "";
		m_message = "normal recording";
	}

	m_chunk_time = 0;
	return chunk();
}

// make another chunked recording
// first stop current recording in case there is one. Then start another recording by chunk prefix.
// return 0 on success
int VideoRecorder::chunk()
{
	// to check the chunk setting
	if (m_chunk_prefix.empty() || !m_chunk_interval)
	{
		m_err = -1;
		m_message = "Invalid chunk settings.";
		return m_err;
	}

	// uses m_chunk_time as an indicateor of first recording
	if (m_chunk_time)
	{
		close();

		if (m_err)
		{
			return m_err;
		}
	}

	// set next chunk time, at x:xx:00 if wall clock alignment is set
	m_chunk_time = m_flag_wclk ? (av_gettime() / 1000 / m_chunk_interval + 1) * m_chunk_interval : av_gettime() / 1000 + m_chunk_interval; 
		
	// file name is set as <prefix><yyyy-MM-dd-hhmmss>.<ext>
	m_url = m_chunk_prefix + get_date_time() + "." + m_format;

	// try to solve the 
	AVStream* st;
	for (int i = 0; i < m_ofmt_Ctx->nb_streams; i++)
	{
		st = m_ofmt_Ctx->streams[i];
		//m_ofmt_Ctx->streams[i]->start_time = AV_NOPTS_VALUE;
		//m_ofmt_Ctx->streams[i]->duration = AV_NOPTS_VALUE;
		//m_ofmt_Ctx->streams[i]->first_dts = AV_NOPTS_VALUE;
		//m_ofmt_Ctx->streams[i]->cur_dts = AV_NOPTS_VALUE;
		st->start_time = AV_NOPTS_VALUE;
		st->duration = static_cast<int64_t>(m_chunk_interval) * 90;
		st->first_dts = AV_NOPTS_VALUE;
		st->cur_dts = 0;
	}
	m_ofmt_Ctx->output_ts_offset = 0;

	if (!(m_ofmt_Ctx->oformat->flags & AVFMT_NOFILE))
	{
		m_err = avio_open(&m_ofmt_Ctx->pb, m_url.c_str(), AVIO_FLAG_WRITE);
		if (m_err < 0)
		{
			m_message.assign(av_err(m_err));
			m_message = "Could not open " + m_url + " with error " + m_message;
			return m_err;
		}
	}
	else
	{
		m_err = -1;
		m_message = "Error: Previous recording is not closed.";
		return m_err;
	}

	//m_err = avformat_write_header(m_ofmt_Ctx, &dictionary);
	m_err = avformat_write_header(m_ofmt_Ctx, &m_options);
	if (m_err < 0)
	{
		m_message.assign(av_err(m_err));
		m_message += " Could not open " + m_url;
		return m_err;
	}

	m_message = m_url + " is openned with return code " + std::to_string(m_err);
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

		if (m_pts_offset_video == 0)
		{
			m_pts_offset_video = -pkt->pts;
		}
		pkt->pts += m_pts_offset_video;
		pkt->dts += m_pts_offset_video;
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
	}
	av_packet_unref(pkt);

	m_message = "packet written";
	if (m_err)
	{
		m_message.assign(av_err(m_err));
		return m_err;
	}

	m_err = 0;
	int64_t t = av_gettime() / 1000;
	if (m_chunk_time && t >= m_chunk_time)
	{
		m_err = chunk();
	}
	//m_err = m_chunk_time && av_gettime() / 1000 >= m_chunk_time ? re_open() : 0;

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

// get the recording filename or url
std::string VideoRecorder::get_url()
{
	return m_url;
}

// This is the sub thread that captures the video streams from specified IP camera and saves them into the circular buffer
// void* videoCapture(void* myptr)
DWORD WINAPI videoCapture(LPVOID myPtr)
{
	AVPacket pkt;

	int ret;

	AVRational tb = ipCam->get_stream_time_base();
	tb.num *= 1000; // change the time base to be ms based

	// read packets from IP camera and save it into circular buffer
	while (true)
	{
		//ret = av_read_frame(ifmt_Ctx, &pkt); // read a frame from the camera
		ret = ipCam->read_packet(&pkt);

		// handle the timeout, blue screen shall be added here
		if (ret < 0)
		{
			fprintf(stderr, "%s.\n", ipCam->get_error_message().c_str());
			continue;
		}

		ret = cbuf->push_packet(&pkt);  // add the packet to the circular buffer
		if (ret > 0)
		{
			if (Debug > 2)
			{
				fprintf(stderr, "Added a new packet (%lldms, %d). The circular buffer has %d packets with size %d now.\n",
					pkt.pts * tb.num / tb.den, pkt.size, ret, cbuf->get_size());
			}
		}
		else
		{
			fprintf(stderr, "Error %s while push new packet into the circular buffer.\n", cbuf->get_error_message().c_str());
		}
		av_packet_unref(&pkt); // handle the release of the packet here
	}
}

int main(int argc, char** argv)
{
	// The IP camera
	//std::string CameraPath = "rtsp://10.25.50.20/h264";
	std::string CameraPath = "rtsp://10.0.9.113:8554/0";
	//CameraPath = "video=Logitech HD Pro Webcam C920";
	//std::string CameraPath = "rtsp://10.0.0.18/h264";
	std::string filename_bg = ""; // file name of background recording
	std::string filename_mn = ""; // file name of main recording

	if (argc > 1)
		CameraPath.assign(argv[1]);

	fprintf(stderr, "Now starting the test...\n");

	ipCam = new Camera();

	// ip camera options
	ipCam->set_options("buffer_size", "200000");
	ipCam->set_options("rtsp_transport", "tcp");
	ipCam->set_options("stimeout", "100000");

	// USB camera options
	//ipCam->set_options("video_size", "1280x720");
	//ipCam->set_options("framerate", "30");
	//ipCam->set_options("vcodec", "h264");

	int ret = ipCam->open(CameraPath);
	if (ret < 0)
	{
		fprintf(stderr, "Could not open IP camera at %s with error %s.\n", CameraPath.c_str(), ipCam->get_error_message().c_str());
		exit(1);
	}

	//int ret = avformat_network_init();
	AVFormatContext* ifmt_Ctx = ipCam->get_input_format_context();
	if (!ifmt_Ctx)
	{
		fprintf(stderr, "Error: %s", ipCam->get_error_message());
		exit(1);
	}

	// Debug only, output the camera information
	if (Debug > 0)
	{
		av_dump_format(ifmt_Ctx, 0, CameraPath.c_str(), 0);
	}

	// Open a circular buffer
	cbuf = new CircularBuffer();
	cbuf->open(30, 100 * 1000 * 1000); // 30s and 100M
	cbuf->add_stream(ifmt_Ctx->streams[0]);

	VideoRecorder* bg_recorder = new VideoRecorder();
	//ret = bg_recorder->add_stream(ifmt_Ctx->streams[0]);
	ret = bg_recorder->add_stream(ipCam->get_stream(ipCam->get_video_index()));

	VideoRecorder* mn_recorder = new VideoRecorder();
	ret = mn_recorder->add_stream(ifmt_Ctx->streams[0]);

	int64_t ChunkTime_bg = 0;  // Chunk time for background recording
	int64_t ChunkTime_mn = 0;  // Chunk time for main recording
	int64_t CurrentTime;
	int number_bg = 0;
	int number_mn = 0;

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
	av_usleep(10 * 1000 * 1000); // sleep for a while to have the circular buffer accumulated

	AVPacket pkt;
	AVRational timebase = cbuf->get_time_base();
	int64_t pts0 = 0;
	bool no_data = true;
	bool main_recorder_recording = false;

	bg_recorder->set_options("movflags", "frag_keyframe");
	bg_recorder->set_options("format", "mp4"); // self defined option

	mn_recorder->set_options("movflags", "frag_keyframe");
	int64_t MainStartTime = av_gettime() / 1000 + 15000;
	ChunkTime_mn = MainStartTime - 100;

	// Open a chunked recording for background recording, where chunk time is 60s
	ret = bg_recorder->open(prefix_videofile + "background-", 60);
	filename_bg = bg_recorder->get_url();
	av_dump_format(bg_recorder->get_output_format_context(), 0, filename_bg.c_str(), 1);

	while (true)
	{
		CurrentTime = av_gettime() / 1000;  // read current time in miliseconds
		no_data = true;

		// read a background packet from the queue
		ret = cbuf->peek_packet(&pkt);
		if (ret > 0)
		{
			if (pts0 == 0)
			{
				pts0 = pkt.pts;
				if (Debug > 1)
				{
					fprintf(stderr, "The first packet: pts=%lld, pts_time=%lld \n",
						pts0, pts0 * timebase.num / timebase.den);
				}
			}

			if (Debug > 2)
			{
				fprintf(stderr, "Read a background packet pts time: %lldms, dt: %lldms, packet size %d, total size: %d.\n",
					1000 * pkt.pts * timebase.num / timebase.den,
					1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size, ret);
			}

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
			no_data = false;
		}

		// arbitrary set main recording starts 15s later
		if (CurrentTime <= MainStartTime)
		{
			if (no_data)
			{
				av_usleep(1000 * 20); //sleep for 20ms
			}
			continue;
		}

		if (!main_recorder_recording)
		{
			//filename_mn = prefix_videofile + "main-" + get_date_time() + ".mp4";
			ret = mn_recorder->open(prefix_videofile + "main-", 3600);
			av_dump_format(mn_recorder->get_output_format_context(), 0, mn_recorder->get_url().c_str(), 1);
			main_recorder_recording = true;
			ChunkTime_mn = CurrentTime + 60000;
		}

		// simulate the external chunk signal
		if (ChunkTime_mn && CurrentTime > ChunkTime_mn)
		{
			ChunkTime_mn += 120000;
			mn_recorder->chunk();
			av_dump_format(mn_recorder->get_output_format_context(), 0, mn_recorder->get_url().c_str(), 1);
			fprintf(stderr, "Main recording get chunked.\n");
		}

		// handle the main stream reading
		ret = cbuf->peek_packet(&pkt, false);
		if (ret > 0)
		{
			if (Debug > 2)
			{
				fprintf(stderr, "Read a main packet pts time: %lld, dt: %lldms, packet size %d, total size: %d.\n",
					pkt.pts * timebase.num / timebase.den,
					1000 * (pkt.pts - pts0) * timebase.num / timebase.den, pkt.size, ret);
			}

			if (mn_recorder->record(&pkt) < 0)
			{
				fprintf(stderr, "%s muxing packet in %s.\n",
					mn_recorder->get_error_message().c_str(),
					filename_mn.c_str());
				break;
			}
			//av_packet_unref(&pkt);

			no_data = false;
		}

		// sleep to reduce the cpu usage
		if (no_data)
		{
			av_usleep(1000 * 20); // sleep for extra 20ms when there is no more background reading
		}
		else
		{
			av_usleep(1000 * 5); // sleep for 5ms}
		}
	}

	if (ret < 0)
		fprintf(stderr, " with error %s.\n", av_err(ret));

}

