alter table rating_tree_nodes
  add column if not exists display_number text;

do $$
begin
  if not exists (
    select 1
    from pg_constraint
    where conname = 'rating_tree_nodes_display_number_check'
      and conrelid = 'rating_tree_nodes'::regclass
  ) then
    alter table rating_tree_nodes
      add constraint rating_tree_nodes_display_number_check
      check (display_number is null or length(btrim(display_number)) > 0);
  end if;
end
$$;

comment on column rating_tree_nodes.display_number is
  '评定树发布的显式显示编号；旧包可为空，2.0 及后续包不再从 node_key 推导编号。';
