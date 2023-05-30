/**************************************************************************//**
*
* @copyright (C) 2019 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Change Logs:
* Date            Author       Notes
* 2023-4-26       Wayne        First version
*
******************************************************************************/

#include <rtthread.h>

#if defined(BSP_USING_VDE)

#include "vc8000_lib.h"
#include "drv_common.h"

#include <dfs_posix.h>

#define DBG_ENABLE
#define DBG_LEVEL DBG_LOG
#define DBG_SECTION_NAME  "vde"
#define DBG_COLOR
#include <rtdbg.h>

#define PATH_H264_INCBIN          ".//default.264"
#define PATH_JPEG_INCBIN          ".//default.jpg"

INCBIN(vde_h264, PATH_H264_INCBIN);
INCBIN(vde_jpeg, PATH_JPEG_INCBIN);

#define THREAD_PRIORITY   5
#define THREAD_STACK_SIZE 10240
#define THREAD_TIMESLICE  5
#define DEF_LAYER_NAME    "lcd"

#define DEF_ROW     1
#define DEF_COLUM   DEF_ROW
#define DEF_MAX_DECODING_INSTANCE   1
#define DEF_H264D_ONLY     1

typedef struct
{
    int i32RowNum;
    int i32ColumNum;
    int i32Index;
    int Reserved;
} S_VDE_VIWE_PARAM;

static uint32_t fbsize, fbaddr, fbpicese;
static rt_device_t psDevLcd = RT_NULL;
static struct pp_params pp = {0};

/*
To switch different video frame buffer to avoid tearing issue.
*/
static void my_pp_done(int handle, void *fbaddr)
{
    int ret;

    if (!psDevLcd || !fbaddr) return;

    /* vsync-after.*/
    ret = rt_device_control(psDevLcd, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL);
    if (ret != 0)
    {
        LOG_E("Wait VSYNC error: %d\n", ret);
        goto exit_my_pp_done;
    }

    /* Use PANDISPLAY without H/W copying */
    ret = rt_device_control(psDevLcd, RTGRAPHIC_CTRL_PAN_DISPLAY, fbaddr);
    if (ret != 0)
    {
        LOG_E("PAN error: %d\n", ret);
        goto exit_my_pp_done;
    }

exit_my_pp_done:

    return;
}

static void vde_decoder(void *parameter)
{
    int ret;
    uint8_t   *file_ptr;
    int   handle = -1;

    S_VDE_VIWE_PARAM sVDEViewParam = *((S_VDE_VIWE_PARAM *)parameter);

    psDevLcd = rt_device_find(DEF_LAYER_NAME);
    struct rt_device_graphic_info gfx_info;
    if (psDevLcd == RT_NULL)
    {
        LOG_E("Can't find %s", DEF_LAYER_NAME);
        goto exit_vde_decoder;
    }

    /* Get LCD Info */
    ret = rt_device_control(psDevLcd, RTGRAPHIC_CTRL_GET_INFO, &gfx_info);
    if (ret != RT_EOK)
    {
        LOG_E("Can't get LCD info %s", DEF_LAYER_NAME);
        goto exit_vde_decoder;
    }

    LOG_I("LCD Width: %d",   gfx_info.width);
    LOG_I("LCD Height: %d",  gfx_info.height);
    LOG_I("LCD bpp:%d",   gfx_info.bits_per_pixel);
    LOG_I("LCD pixel format:%d",   gfx_info.pixel_format);
    LOG_I("LCD frame buffer@0x%08x",   gfx_info.framebuffer);
    LOG_I("LCD frame buffer size:%d",   gfx_info.smem_len);

    fbsize = (gfx_info.width * gfx_info.height * gfx_info.bits_per_pixel / 8);
    fbpicese = gfx_info.smem_len / fbsize;
    fbaddr = (uint32_t)gfx_info.framebuffer;

    /* open lcd */
    ret = rt_device_open(psDevLcd, 0);
    if (ret != RT_EOK)
    {
        LOG_E("Can't open %s", DEF_LAYER_NAME);
        goto exit_vde_decoder;
    }

    pp.frame_buf_w = gfx_info.width;
    pp.frame_buf_h = gfx_info.height;
    pp.img_out_x = (gfx_info.width / sVDEViewParam.i32RowNum) * (sVDEViewParam.i32Index % sVDEViewParam.i32RowNum);
    pp.img_out_y = (gfx_info.height / sVDEViewParam.i32ColumNum) * (sVDEViewParam.i32Index / sVDEViewParam.i32ColumNum);
    pp.img_out_w = gfx_info.width / sVDEViewParam.i32RowNum;
    pp.img_out_h = gfx_info.height / sVDEViewParam.i32ColumNum;
#if 1
    pp.img_out_fmt = VC8000_PP_F_RGB888;
#else
    {
        pp.img_out_fmt = VC8000_PP_F_RGB565;   // bad color
        //pp.img_out_fmt = VC8000_PP_F_YUV422; // fail
        //pp.img_out_fmt = VC8000_PP_F_NV12;   // fail
        int pixfmt = RTGRAPHIC_PIXEL_FORMAT_RGB565;
        ret = rt_device_control(psDevLcd, RTGRAPHIC_CTRL_SET_MODE, &pixfmt);
        if (ret != RT_EOK)
        {
            LOG_E("Can't set mode %s", DEF_LAYER_NAME);
            goto exit_vde_decoder;
        }
    }
#endif

    pp.rotation = VC8000_PP_ROTATION_NONE;
    pp.pp_out_dst = fbaddr;
    pp.pp_done_cb = my_pp_done;

    LOG_I("[%dx%d #%d] %d %d %d %d out:%08x",
          sVDEViewParam.i32RowNum,
          sVDEViewParam.i32ColumNum,
          sVDEViewParam.i32Index,
          pp.img_out_x, pp.img_out_y, pp.img_out_w, pp.img_out_h, pp.pp_out_dst);

    LOG_I("START decoding.");

    while (1)
    {
        uint32_t total_len, counter;
        uint32_t bs_len, remain;

        if (DEF_H264D_ONLY || sVDEViewParam.i32Index & 0x1)
        {
            handle = VC8000_Open(VC8000_CODEC_H264);
        }
        else
        {
            handle = VC8000_Open(VC8000_CODEC_JPEG);
        }

        if (handle < 0)
        {
            // LOG_W("VC8000_Open failed! (%d)", handle);
            continue;
        }


        if (DEF_H264D_ONLY || sVDEViewParam.i32Index & 0x1)
        {
            file_ptr = (uint8_t *)&incbin_vde_h264_start;
            bs_len = (uint32_t)((char *)&incbin_vde_h264_end - (char *)&incbin_vde_h264_start);
        }
        else
        {
            file_ptr = (uint8_t *)&incbin_vde_jpeg_start;
            bs_len = (uint32_t)((char *)&incbin_vde_jpeg_end - (char *)&incbin_vde_jpeg_start);
        }

        total_len = bs_len;
        counter = 0;

        do
        {
            /*
            To switch different video frame buffer to avoid tearing issue.
            */
            pp.pp_out_dst = (uint32_t)fbaddr + (counter % fbpicese) * fbsize;
            ret = VC8000_PostProcess(handle, &pp);
            if (ret < 0)
            {
                LOG_E("VC8000_PostProcess failed! (%d)\n", ret);
                goto exit_vde_decoder;
            }

            ret = VC8000_Decode(handle, file_ptr, bs_len, RT_NULL, &remain);
            if (ret != 0)
            {
                LOG_E("VC8000_Decode error: %d\n", ret);
                goto exit_vde_decoder;
            }

            file_ptr += (bs_len - remain);
            bs_len = remain;
            counter++;

            //LOG_I("[%d-%d]. (%d/%d) %d ", handle, counter, bs_len, total_len, remain);
        }
        while (remain > 0);

        LOG_I("%s(%d) decode done.", (DEF_H264D_ONLY || (sVDEViewParam.i32Index & 0x1)) ? "H264" : "JPEG", handle);

        if (handle >= 0)
        {
            VC8000_Close(handle);
            handle = -1;
        }

    } //while(1)

exit_vde_decoder:

    if (handle >= 0)
    {
        VC8000_Close(handle);
        handle = -1;
    }

    if (psDevLcd)
        rt_device_close(psDevLcd);

    return;
}

static int vde_demo(void)
{
    int idx;
    S_VDE_VIWE_PARAM sVDEViewParam = { DEF_ROW, DEF_COLUM, DEF_MAX_DECODING_INSTANCE, 0};

    for (idx = 0; idx < DEF_MAX_DECODING_INSTANCE; idx++)
    {
        sVDEViewParam.i32Index = idx;

        rt_thread_t vde_thread = rt_thread_create("vdedemo",
                                 vde_decoder,
                                 &sVDEViewParam,
                                 THREAD_STACK_SIZE,
                                 THREAD_PRIORITY,
                                 THREAD_TIMESLICE);

        if (vde_thread != RT_NULL)
            rt_thread_startup(vde_thread);
    }

    return 0;
}

MSH_CMD_EXPORT(vde_demo, vde player);
//INIT_APP_EXPORT(vde_demo);

#endif
