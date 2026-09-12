"""把归档原图重采样成版面尺寸的副本（设计 §11.4）。

归档里的照片是证据，必须原样保留；但报告里那张只有 6.9cm 宽，原样嵌进去等于把
一份 1600×1200 的图塞进 2.7 英寸的框。实测 455 张照片的定期检测报告因此做到
126MB——收件箱发不出去，老机器上 Word 打开也吃力。

所以生成时按目标印刷分辨率重新采样一份，只用于嵌入；归档文件一个字节都不动。

顺带修一件事：JPEG 的 EXIF 方向标记 python-docx 不认，竖拍的照片原样嵌进去会躺着。
重采样时按 EXIF 转正，存下来的副本就不带方向标记了。

这一层只决定"嵌哪个文件"，不管尺寸怎么摆——装框仍由调用方按返回的文件重新量，
免得两处各算一遍图片尺寸。
"""

from __future__ import annotations

from pathlib import Path

from PIL import ExifTags, Image, ImageOps


#: 嵌入照片的目标印刷分辨率。
#:
#: 200 dpi：6.9cm 宽的框对应 543 像素。照片是连续色调不是线条图，200 dpi 在纸面上
#: 与 300 dpi 看不出差别，文件却小一半。要更清晰就调高这个数——它是这个模块唯一的
#: 画质旋钮。
TARGET_DPI = 200

#: JPEG 重新编码质量。85 是"看不出损失"与"体积可控"的常规折中。
JPEG_QUALITY = 85

#: EXIF 里表示"不用转"的方向值（1 是正的，0 表示没有这个标记）。
UPRIGHT_ORIENTATIONS = (0, 1)


def target_pixels(frame_width_emu: int, frame_height_emu: int) -> tuple[int, int]:
    """版面框尺寸（EMU）换算成目标像素。1 英寸 = 914400 EMU。"""
    return (
        max(1, round(frame_width_emu / 914400 * TARGET_DPI)),
        max(1, round(frame_height_emu / 914400 * TARGET_DPI)),
    )


def resample(
    source: Path, frame_width_emu: int, frame_height_emu: int, cache_dir: Path
) -> Path:
    """按版面框重采样 source，返回该嵌入的文件。

    不需要缩小、也不需要转正时返回 source 本身，省一次重新编码。Pillow 打不开时
    同样返回 source——判定"这不是图片"是调用方的事，这一层不替它决定；真正读不了的
    文件会在 add_picture 那里以 report_photo_unreadable 中止。
    """
    try:
        with Image.open(source) as image:
            orientation = image.getexif().get(ExifTags.Base.Orientation, 0)
            width, height = image.size
            box_width, box_height = target_pixels(frame_width_emu, frame_height_emu)
            scale = min(box_width / width, box_height / height, 1.0)

            if scale >= 1.0 and orientation in UPRIGHT_ORIENTATIONS:
                return source

            upright = ImageOps.exif_transpose(image)
            size = (
                max(1, round(upright.width * scale)),
                max(1, round(upright.height * scale)),
            )
            resized = (
                upright.resize(size, Image.LANCZOS) if size != upright.size else upright
            )
            if resized.mode not in ("RGB", "L"):
                resized = resized.convert("RGB")

            cache_dir.mkdir(parents=True, exist_ok=True)
            # 文件名带上源文件名，出问题时能直接对回归档。
            destination = cache_dir / f"{source.stem}.jpg"
            index = 1
            while destination.exists():
                destination = cache_dir / f"{source.stem}-{index}.jpg"
                index += 1
            resized.save(destination, "JPEG", quality=JPEG_QUALITY, optimize=True)
            return destination
    except OSError:
        return source
