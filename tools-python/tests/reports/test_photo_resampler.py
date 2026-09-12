"""照片重采样（设计 §11.4）。

守两件事：归档原图一个字节都不动，以及嵌进报告的那张确实小下来了——实测 455 张
原图会做出一份 126MB 的报告。
"""

from __future__ import annotations

from pathlib import Path

import pytest
from docx.shared import Mm
from PIL import Image

from bridge_report_tools.reports.photo_resampler import (
    TARGET_DPI,
    resample,
    target_pixels,
)


FRAME_WIDTH = int(Mm(69))
FRAME_HEIGHT = int(Mm(52))


def jpeg(path: Path, width: int, height: int, orientation: int | None = None) -> Path:
    image = Image.new("RGB", (width, height), (120, 130, 140))
    # 造点纹理，免得纯色图被 JPEG 压到看不出尺寸差别。
    for x in range(0, width, 7):
        for y in range(0, height, 11):
            image.putpixel((x, y), ((x * 7) % 255, (y * 3) % 255, 90))
    if orientation is None:
        image.save(path, "JPEG", quality=92)
        return path
    exif = image.getexif()
    exif[274] = orientation  # 274 = EXIF Orientation
    image.save(path, "JPEG", quality=92, exif=exif)
    return path


def size_of(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def test_large_photo_is_downsampled_to_the_frame(tmp_path: Path) -> None:
    source = jpeg(tmp_path / "big.jpg", 1600, 1200)

    result = resample(source, FRAME_WIDTH, FRAME_HEIGHT, tmp_path / "cache")

    assert result != source
    box_width, box_height = target_pixels(FRAME_WIDTH, FRAME_HEIGHT)
    width, height = size_of(result)
    assert width <= box_width and height <= box_height
    # 比例不变：装框那一步靠它，拉伸了照片就变形了（设计 §11.4）。
    assert width / height == pytest.approx(1600 / 1200, rel=1e-2)
    assert result.stat().st_size < source.stat().st_size


def test_archived_original_is_never_touched(tmp_path: Path) -> None:
    """归档里的照片是证据，重采样只产出副本。"""
    source = jpeg(tmp_path / "big.jpg", 1600, 1200)
    before = source.read_bytes()

    resample(source, FRAME_WIDTH, FRAME_HEIGHT, tmp_path / "cache")

    assert source.read_bytes() == before
    assert size_of(source) == (1600, 1200)


def test_small_photo_is_used_as_is(tmp_path: Path) -> None:
    """原图已经不比版面框大就别再编码一次——放大只会变糊，还更占地方。"""
    source = jpeg(tmp_path / "small.jpg", 200, 150)

    assert resample(source, FRAME_WIDTH, FRAME_HEIGHT, tmp_path / "cache") == source


def test_rotated_photo_is_turned_upright(tmp_path: Path) -> None:
    """python-docx 不认 EXIF 方向标记，竖拍的照片原样嵌进去会躺着。"""
    source = jpeg(tmp_path / "rotated.jpg", 1600, 1200, orientation=6)

    result = resample(source, FRAME_WIDTH, FRAME_HEIGHT, tmp_path / "cache")

    width, height = size_of(result)
    assert height > width  # 转正后变成竖图


def test_small_but_rotated_photo_is_still_rewritten(tmp_path: Path) -> None:
    """小图不用缩，但方向还是要转——不能因为"不用缩"就把躺着的图放过去。"""
    source = jpeg(tmp_path / "small-rotated.jpg", 200, 150, orientation=6)

    result = resample(source, FRAME_WIDTH, FRAME_HEIGHT, tmp_path / "cache")

    assert result != source
    assert size_of(result) == (150, 200)


def test_unreadable_file_falls_back_to_the_source(tmp_path: Path) -> None:
    """判定"这不是图片"是调用方的事；这一层不替它决定，交回原路径即可。"""
    source = tmp_path / "broken.jpg"
    source.write_bytes(b"not an image")

    assert resample(source, FRAME_WIDTH, FRAME_HEIGHT, tmp_path / "cache") == source


def test_target_pixels_follow_the_configured_dpi() -> None:
    width, height = target_pixels(int(Mm(69)), int(Mm(52)))

    # 69mm = 2.717 英寸；200dpi 下约 543 像素。
    assert width == round(69 / 25.4 * TARGET_DPI)
    assert height == round(52 / 25.4 * TARGET_DPI)


def test_two_photos_with_the_same_name_do_not_collide(tmp_path: Path) -> None:
    """不同目录下同名的归档照片不能互相覆盖，否则报告里会出现重复的图。"""
    (tmp_path / "a").mkdir()
    (tmp_path / "b").mkdir()
    first = jpeg(tmp_path / "a" / "photo.jpg", 1600, 1200)
    second = jpeg(tmp_path / "b" / "photo.jpg", 1200, 1600)
    cache = tmp_path / "cache"

    left = resample(first, FRAME_WIDTH, FRAME_HEIGHT, cache)
    right = resample(second, FRAME_WIDTH, FRAME_HEIGHT, cache)

    assert left != right
    assert size_of(left)[0] > size_of(left)[1]
    assert size_of(right)[1] > size_of(right)[0]
