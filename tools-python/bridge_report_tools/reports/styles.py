"""模板必须提供的命名样式。

Docx Builder 插入动态内容时只用这些名字，不在代码里散落字体、字号和边距常量
（设计 §18）。模板换版式时改的是模板里这些样式的定义，生成器代码不动。

这些名字属于模板契约，因此留在包里；具体怎么把它们画出来是模板作者（或
scripts/report-template 下的生成器）的事。
"""

from __future__ import annotations

from typing import Final


STYLE_BODY: Final = "报告正文"
STYLE_TABLE_CAPTION: Final = "报告表题"
STYLE_TABLE_HEADER: Final = "报告表头"
STYLE_TABLE_CELL: Final = "报告表格文字"

#: 附录的卡片用小五（9pt），比正文表格的五号小一号。
#:
#: 两张卡片的格子远比正文表格密——附录1 一行三对「标签/取值」，附录2 一行三组
#: 「编号/名称/取值」还带联系电话，五号字排不下就会到处折行。数字仍是 Times New
#: Roman，与其余样式一致。
STYLE_CARD_HEADER: Final = "报告卡片表头"
STYLE_CARD_CELL: Final = "报告卡片文字"
#: 附录2 卡片里「A 桥梁所处行政区划代码」这类分段行：整行合并、靠左。
STYLE_CARD_BAND: Final = "报告卡片分段"
STYLE_PHOTO: Final = "报告照片"
STYLE_PHOTO_CAPTION: Final = "报告图题"
STYLE_COVER_TITLE: Final = "报告封面标题"
STYLE_COVER_FIELD: Final = "报告封面字段"
STYLE_NOTICE: Final = "报告声明条款"

#: 表格样式（不是段落样式）：病害表、人员表、设备表用的带框线表格。
STYLE_TABLE: Final = "报告表格"
#: 照片两栏版式用的无框线表格。版式靠表格实现，框线不能画出来。
STYLE_PHOTO_LAYOUT: Final = "报告图片版式"

#: 生成器装配动态内容时会用到的样式，模板缺任何一个都没法出报告。
REQUIRED_BUILDER_STYLES: Final = frozenset(
    {
        STYLE_BODY,
        STYLE_TABLE_CAPTION,
        STYLE_TABLE_HEADER,
        STYLE_TABLE_CELL,
        STYLE_CARD_HEADER,
        STYLE_CARD_CELL,
        STYLE_CARD_BAND,
        STYLE_PHOTO,
        STYLE_PHOTO_CAPTION,
        STYLE_TABLE,
        STYLE_PHOTO_LAYOUT,
    }
)
