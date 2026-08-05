"""读取来源软件桌面程序的本机离线库。

来源软件把整座桥的检测数据下载到 Electron 的 WebSQL 库里，其中每条病害自带评定指标、
标度、拆好的尺寸与所属构件，照片按外键绑定。本模块只读取，不写回。

这是对方的内部实现而不是对外接口，表结构可能随其版本升级而变。因此打开时先校验表与
列存在，缺失立刻报错——**绝不静默产出残缺数据**。
"""

from __future__ import annotations

import base64
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path


class SourceDatabaseError(Exception):
    """带错误码的来源库异常，供上层映射成 HTTP 响应。"""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


#: 只列实际用到的列。实测三年百股大桥的数据里，高度、百分比、起终点坐标、
#: posPart1~5 全为空，不纳入模型；用得上时再加，免得对着 53 列做无谓的校验。
REQUIRED_SCHEMA: dict[str, tuple[str, ...]] = {
    "tasks": ("id", "name", "checkDate"),
    "taskTrees": ("id", "taskId", "name", "levelCode", "nodeType", "memberCount", "memberTypeName"),
    "outerCheckData": (
        "id", "taskId", "treeId", "name", "data", "pos", "degree",
        "judgeIndexId", "judgeTreeId", "diseaseDefinition",
        "数量", "数量单位", "长度", "长度单位", "宽度", "宽度单位", "面积一", "面积一单位", "走向",
    ),
    "images": ("id", "taskId", "fileName", "contentType", "w", "h", "ForeignTable", "ForeignKey", "memberNum"),
    "judgeIndex": ("id", "tableNum", "name"),
}

#: 尺寸列与其单位列成对出现。
DIMENSION_COLUMNS: tuple[tuple[str, str], ...] = (
    ("数量", "数量单位"),
    ("长度", "长度单位"),
    ("宽度", "宽度单位"),
    ("面积一", "面积一单位"),
)


@dataclass(frozen=True)
class SourceTask:
    id: str
    name: str
    check_date: str | None


@dataclass(frozen=True)
class ComponentNode:
    id: str
    name: str
    level_code: str
    #: 1 部位 / 2 构件类别 / 3 具体构件
    node_type: int
    member_count: int | None
    member_type_name: str | None

    @property
    def parent_level_code(self) -> str:
        """来源库没有 parentId，父级只能靠层级码前缀推。"""
        return self.level_code[:-3] if len(self.level_code) > 3 else ""


@dataclass(frozen=True)
class SourceDefect:
    id: str
    tree_id: str
    #: 病害类型。来源软件允许不选，空串是合法值，不是脏数据。
    name: str
    description: str
    position: str
    degree: int | None
    judge_index_id: str
    judge_tree_id: str
    template_definition: str
    dimensions: dict[str, tuple[str, str]] = field(default_factory=dict)
    direction: str = ""


@dataclass(frozen=True)
class SourcePhoto:
    id: str
    defect_id: str
    content_type: str
    width: int | None
    height: int | None
    member_number: str


def _table_columns(db: sqlite3.Connection, table: str) -> set[str]:
    return {row[1] for row in db.execute(f'pragma table_info("{table}")')}


def open_source_db(path: str | Path) -> sqlite3.Connection:
    """只读打开来源库并校验表结构。"""
    target = Path(path)
    if not target.is_file():
        raise SourceDatabaseError("source_db_not_found", f"离线库不存在：{target}")
    connection = sqlite3.connect(f"file:{target.as_posix()}?mode=ro", uri=True)
    try:
        present = {row[0] for row in connection.execute(
            "select name from sqlite_master where type='table'")}
        for table, columns in REQUIRED_SCHEMA.items():
            if table not in present:
                raise SourceDatabaseError(
                    "source_db_table_missing", f"离线库缺少表 {table}，来源软件版本可能已变更。")
            missing = [c for c in columns if c not in _table_columns(connection, table)]
            if missing:
                raise SourceDatabaseError(
                    "source_db_column_missing",
                    f"离线库的表 {table} 缺少列 {', '.join(missing)}，来源软件版本可能已变更。")
    except Exception:
        connection.close()
        raise
    return connection


def load_task(db: sqlite3.Connection, task_id: str) -> SourceTask:
    row = db.execute("select id, name, checkDate from tasks where id=?", (task_id,)).fetchone()
    if row is None:
        raise SourceDatabaseError(
            "source_task_not_found",
            f"离线库里没有检测任务 {task_id}。请先在来源软件的桌面程序里打开该桥，数据才会下载到本机。")
    return SourceTask(str(row[0]), str(row[1] or ""), row[2] or None)


def load_component_tree(db: sqlite3.Connection, task_id: str) -> list[ComponentNode]:
    rows = db.execute(
        "select id, name, levelCode, nodeType, memberCount, memberTypeName"
        " from taskTrees where taskId=? order by levelCode, id", (task_id,))
    return [
        ComponentNode(str(i), str(n or ""), str(lv or ""), int(nt or 0),
                      int(mc) if mc is not None else None, mt or None)
        for i, n, lv, nt, mc, mt in rows
    ]


def load_defects(db: sqlite3.Connection, task_id: str) -> list[SourceDefect]:
    columns = ", ".join(f'"{c}"' for c in REQUIRED_SCHEMA["outerCheckData"])
    rows = db.execute(
        f"select {columns} from outerCheckData where taskId=? order by treeId, id", (task_id,))
    index = {name: position for position, name in enumerate(REQUIRED_SCHEMA["outerCheckData"])}
    defects = []
    for row in rows:
        dimensions = {}
        for value_column, unit_column in DIMENSION_COLUMNS:
            value = row[index[value_column]]
            if value in (None, ""):
                continue
            dimensions[value_column] = (str(value), str(row[index[unit_column]] or ""))
        defects.append(SourceDefect(
            id=str(row[index["id"]]),
            tree_id=str(row[index["treeId"]] or ""),
            name=str(row[index["name"]] or ""),
            description=str(row[index["data"]] or ""),
            position=str(row[index["pos"]] or ""),
            degree=int(row[index["degree"]]) if row[index["degree"]] is not None else None,
            judge_index_id=str(row[index["judgeIndexId"]] or ""),
            judge_tree_id=str(row[index["judgeTreeId"]] or ""),
            template_definition=str(row[index["diseaseDefinition"]] or ""),
            dimensions=dimensions,
            direction=str(row[index["走向"]] or ""),
        ))
    return defects


def load_indicator_codes(db: sqlite3.Connection) -> dict[str, tuple[str, str]]:
    """`judgeIndex.id` → (指标编号, 指标名称)。

    编号会重复（同一个 `5.1.1-13` 在不同分组下是两个不同指标），所以按 id 索引。
    """
    return {
        str(i): (str(num or ""), str(name or ""))
        for i, num, name in db.execute("select id, tableNum, name from judgeIndex")
    }


def load_photos(db: sqlite3.Connection, task_id: str) -> list[SourcePhoto]:
    """只取元信息。照片本体是 base64，一座桥约 42 MB，列清单时不读进内存。"""
    rows = db.execute(
        "select id, ForeignKey, contentType, w, h, memberNum from images"
        " where taskId=? and ForeignTable='outerCheckData' order by ForeignKey, id", (task_id,))
    return [
        SourcePhoto(str(i), str(fk or ""), str(ct or ""),
                    int(w) if w is not None else None,
                    int(h) if h is not None else None,
                    str(mn or ""))
        for i, fk, ct, w, h, mn in rows
    ]


def load_photo_content(db: sqlite3.Connection, photo_id: str) -> tuple[str, bytes]:
    """按需取一张照片的本体，解出 data URI 里的 base64。"""
    row = db.execute("select contentType, fileName from images where id=?", (photo_id,)).fetchone()
    if row is None:
        raise SourceDatabaseError("source_photo_not_found", f"离线库里没有照片 {photo_id}。")
    content_type = str(row[0] or "")
    payload = str(row[1] or "")
    if "," in payload and payload.startswith("data:"):
        payload = payload.split(",", 1)[1]
    try:
        return content_type, base64.b64decode(payload, validate=False)
    except Exception as error:  # noqa: BLE001 - 统一成带码异常交给上层
        raise SourceDatabaseError("source_photo_decode_failed", f"照片 {photo_id} 无法解码。") from error
