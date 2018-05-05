//#ifdef __cplusplus
//
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
using namespace std;

extern "C"
{
#include <libavutil/motion_vector.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

};
//#endif

#include <cv.h>
#include <highgui.h>
using namespace std;

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static const char *src_filename = NULL;

static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVFrame *pFrameRGB=NULL;
static AVPacket pkt;
static int video_frame_count = 0;
static SwsContext *img_convert_ctx=NULL;
FILE *fp;
static IplImage *imgShow=NULL;
static IplImage *imgShow2=NULL;
typedef struct{
int mv_sx;
int mv_sy;
int mv_x;
int mv_y;
}MV_DATA;

void uchar2IplImageBGR(unsigned char *inArrayCur, int img_w, int img_h,IplImage* pImg);

int img_w,img_h;
const int mbNum=131100;//1920*1080/16+1920/4*2+1080/4*2;
char reslutVideoName[128] = "./result.avi";

CvPoint p1,p2;

CvVideoWriter* writer = NULL;
CvVideoWriter* writer_o = NULL;

static int decode_packet(int *got_frame, int cached)
{
    MV_DATA* mv_data=(MV_DATA*)malloc(mbNum*sizeof(MV_DATA));//读取运动矢量保存到mv_data中

    CvFont font;
    cvInitFont(&font, CV_FONT_HERSHEY_SIMPLEX, 1, 1, 1, 2, 8);

    vector<MV_DATA> mv_datas;

    int decoded = pkt.size;
    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        int ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            //fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        double sum_mv = 0;

        if (*got_frame) {
            int i;
            AVFrameSideData *sd;

            video_frame_count++;
            sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
            if (sd) {

                img_convert_ctx = sws_getContext(video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt, video_dec_ctx->width, video_dec_ctx->height, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL);
                sws_scale(img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, video_dec_ctx->height, pFrameRGB->data, pFrameRGB->linesize);

                uchar2IplImageBGR(pFrameRGB->data[0],video_dec_ctx->width,video_dec_ctx->height,imgShow);
                //cvCopy(imgShow,imgShow2);
                const AVMotionVector *mvs = (const AVMotionVector *)sd->data;

                for (i = 0; i < sd->size / sizeof(*mvs); i++) {
                    const AVMotionVector *mv = &mvs[i];
//                  printf("%d %2d %2d %2d %4d %4d %4d %4d %4d %4d\n",
//                           video_frame_count, mv->source,
//                           mv->w, mv->h, mv->src_x, mv->src_y,
//                           mv->dst_x, mv->dst_y, mv->dst_x-mv->src_x, mv->dst_y-mv->src_y);

                    int dx = (mv->dst_x-mv->src_x) * 1;
                    int dy = (mv->dst_y-mv->src_y) * 1;
                    p1.x=mv->src_x;
                    p1.y=mv->src_y;
                    p2.x=mv->src_x + dx;
                    p2.y=mv->src_y + dy;

                    /*
                    if (dx != 0 || dy != 0)
                      if (dx <= -5)
                      cvLine(imgShow,p1,p2,cvScalar( 0,0,255 ),1,8,0);
                    */

                    if ((abs(dx) > 5 || abs(dy) > 5) && (mv->src_y > video_dec_ctx->height * 0.2))
                        sum_mv += (abs(dx) + abs(dy));

                }
                double avg_mv = sum_mv / img_w / img_h;
                char buf[100];
                sprintf(buf, "%f avg mv", avg_mv);
                printf("%s\n", buf);
                if (avg_mv >= 0.001) {
                    cvWriteFrame(writer_o, imgShow);
                    //cvPutText(imgShow, buf, cvPoint(50, img_h - 50), &font, CV_RGB(255, 0, 0));
                    //cvWriteFrame(writer, imgShow);
                    //cvShowImage("1", imgShow);
                    //cvWaitKey(1);
                }
                sws_freeContext(img_convert_ctx);
            }
        }
    }
    free(mv_data);
    mv_datas.clear();
    return decoded;
}


static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    } else {
        *stream_idx = ret;
        st = fmt_ctx->streams[*stream_idx];

        /* find decoder for the stream */
        dec_ctx = st->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Init the video decoder */
        av_dict_set(&opts, "flags2", "+export_mvs", 0);
        if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
    }

    return 0;
}


int main(int argc, char** argv)
{
    int ret = 0, got_frame;
    src_filename = argv[1];
    //cvNamedWindow("1",0);
    //cvNamedWindow("2",0);
    char out_filename[128];
    char out_filename_o[128];
    strcpy(out_filename, src_filename);
    strcat(out_filename, ".avi");

    strcpy(out_filename_o, src_filename);
    strcat(out_filename_o, ".origin.avi");

    char motionname[] = "MV_Data.txt";
    fp = fopen(motionname, "w");

    av_register_all();

    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        video_stream = fmt_ctx->streams[video_stream_idx];
        video_dec_ctx = video_stream->codec;
    }

    av_dump_format(fmt_ctx, 0, src_filename, 0);

    if (!video_stream) {
        fprintf(stderr, "Could not find video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    frame = av_frame_alloc();
    pFrameRGB=av_frame_alloc();

    uint8_t *out_buffer;
    out_buffer=new uint8_t[avpicture_get_size(AV_PIX_FMT_BGR24, video_dec_ctx->width, video_dec_ctx->height)];
    avpicture_fill((AVPicture *)pFrameRGB, out_buffer, AV_PIX_FMT_BGR24, video_dec_ctx->width, video_dec_ctx->height);
    imgShow = cvCreateImage(cvSize(video_dec_ctx->width,video_dec_ctx->height),IPL_DEPTH_8U,3);
    imgShow2 = cvCreateImage(cvSize(video_dec_ctx->width,video_dec_ctx->height),IPL_DEPTH_8U,3);
    img_w=video_dec_ctx->width;
    img_h=video_dec_ctx->height;

    printf("output file name:%s\n", out_filename);
    //writer = cvCreateVideoWriter(out_filename, CV_FOURCC('D', 'I', 'V', 'X'), 25, cvSize(img_w, img_h), 1);
    writer_o = cvCreateVideoWriter(out_filename_o, CV_FOURCC('D', 'I', 'V', 'X'), 25, cvSize(img_w, img_h), 1);

    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }


    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);

end:

    fclose(fp);
    avcodec_close(video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&pFrameRGB);
    cvReleaseImage(&imgShow);
    cvReleaseVideoWriter(&writer);
    //getchar();
    return ret < 0;

}



 void uchar2IplImageBGR(unsigned char *inArrayCur, int img_w, int img_h,IplImage* pImg)
{
    int i,j;

    for (i = 0; i < img_h; i++)
    {
        for (j = 0; j < img_w*3; j++)
        {
            *(pImg->imageData + i*pImg->widthStep+j)=inArrayCur[(i)*img_w*3+j] ;
        }
    }
}

