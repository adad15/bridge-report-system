-- 029 跨年病害线索的身份改按评定树节点判定
--
-- 此前"同一处病害"的规范键是 (构件, defect_type 文字, 位置)。文字来自 Word 报告原文，
-- 拿它当跨年身份有两个毛病：
--
--   区分度不够。实测这座桥自动匹配上的 275 条病害里，273 条的 defect_type 仍是原文，
--   其中「失效」「破损」这类写法在同一构件上根本分不出是哪种病害。
--
--   一改写法就断链。归一化只处理全角半角、大小写和标点映射，不认同义词——而且
--   `、` 是映射成 `,` 不是删掉，所以「渗水、泛碱」与「渗水泛碱」算两条不同的线索。
--
-- 改用评定树节点：观测入库时必须带一个适用的节点（确认前校验保证），节点本身就是规范
-- 分类，跨年天然对齐。
--
-- 键取 node_key 而不是 rating_tree_node_id：后者每发布一版评定树就是一批新 UUID
-- （实测本库 5 个已发布版本、411 个病害节点，每版全新），拿它做键，哪一年锁了新版树就
-- 全部断链。node_key（形如 org.bridge.defect.5_1_1_1）跨版本不变。
--
-- 位置继续留在键里：同一构件不同位置的病害算不同的病害。

alter table defect_threads
  add column if not exists node_key text;

comment on column defect_threads.node_key is
  '评定树节点的跨版本稳定键（rating_tree_nodes.node_key）。与 bridge_component_id、'
  'defect_location 共同构成"同一处病害"的规范键。defect_type 仅作展示，不参与身份判定。';

-- 存量回填：按线索名下观测的节点取。同一线索的观测理应指向同一个节点，取其一即可；
-- 真出现分歧时留空，交由人工在整理台上重新归并——猜一个填进去比留空更难查。
update defect_threads t
   set node_key = sub.node_key
  from (
        select o.defect_thread_id,
               min(n.node_key) as node_key,
               count(distinct n.node_key) as distinct_keys
          from defect_observations o
          join rating_tree_nodes n on n.id = o.rating_tree_node_id
         where o.defect_thread_id is not null
         group by o.defect_thread_id
       ) sub
 where sub.defect_thread_id = t.id
   and sub.distinct_keys = 1
   and t.node_key is null;

-- 归并查询按 (桥, 构件, 节点, 位置) 找现有线索，给它一条索引。
-- 位置的 null 与空串在业务上是同一个取值（见 ThreadCanonicalKey），索引里统一成空串。
create index if not exists ix_defect_threads_canonical_key
  on defect_threads (bridge_id, bridge_component_id, node_key, coalesce(defect_location, ''));
