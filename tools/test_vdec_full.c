#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <errno.h>

int main(int argc, char **argv) {
    const char *devname = argc > 1 ? argv[1] : "/dev/video0";
    int fd = open(devname, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 1;
    }
    printf("Card: %s, Driver: %s, Caps: 0x%08x\n", cap.card, cap.driver, cap.capabilities);

    /* Enumerate Output formats */
    printf("\n=== OUTPUT_MPLANE formats ===\n");
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        printf("  [%d] %.4s: %s (flags 0x%x)\n",
               fmtdesc.index, (char*)&fmtdesc.pixelformat, fmtdesc.description, fmtdesc.flags);
        fmtdesc.index++;
    }

    /* Enumerate Capture formats */
    printf("\n=== CAPTURE_MPLANE formats ===\n");
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        printf("  [%d] %.4s: %s (flags 0x%x)\n",
               fmtdesc.index, (char*)&fmtdesc.pixelformat, fmtdesc.description, fmtdesc.flags);
        fmtdesc.index++;
    }

    /* Test framesizes for H264 */
    printf("\n=== FRAMESIZES for H264 ===\n");
    struct v4l2_frmsizeenum fsize;
    memset(&fsize, 0, sizeof(fsize));
    fsize.pixel_format = v4l2_fourcc('H', '2', '6', '4');
    fsize.index = 0;
    if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize) == 0) {
        if (fsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
            printf("  Stepwise: %ux%u to %ux%u (step %ux%u)\n",
                   fsize.stepwise.min_width, fsize.stepwise.min_height,
                   fsize.stepwise.max_width, fsize.stepwise.max_height,
                   fsize.stepwise.step_width, fsize.stepwise.step_height);
        } else {
            printf("  Type %u\n", fsize.type);
        }
    } else {
        printf("  VIDIOC_ENUM_FRAMESIZES failed: %s (%d)\n", strerror(errno), errno);
    }

    /* Set Output format (H264, 1920x1080) */
    printf("\n=== Setting OUTPUT_MPLANE to H264 (1920x1080) ===\n");
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width = 1920;
    fmt.fmt.pix_mp.height = 1080;
    fmt.fmt.pix_mp.pixelformat = v4l2_fourcc('H', '2', '6', '4');
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 1920 * 1080;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        printf("  VIDIOC_S_FMT output failed: %s (%d)\n", strerror(errno), errno);
    } else {
        printf("  S_FMT Success: %.4s, %ux%u, planes: %u, sizeimage[0]: %u\n",
               (char*)&fmt.fmt.pix_mp.pixelformat,
               fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
               fmt.fmt.pix_mp.num_planes, fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
    }

    /* Get Capture format (should be MM21 or NV12 negotiated) */
    printf("\n=== Getting CAPTURE_MPLANE ===\n");
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        printf("  VIDIOC_G_FMT capture failed: %s (%d)\n", strerror(errno), errno);
    } else {
        printf("  G_FMT Capture: %.4s, %ux%u, planes: %u, bytesperline[0]: %u, sizeimage[0]: %u\n",
               (char*)&fmt.fmt.pix_mp.pixelformat,
               fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
               fmt.fmt.pix_mp.num_planes, fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
               fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
    }

    close(fd);
    printf("\nTest completed successfully.\n");
    return 0;
}
