"""Independently recompute the isolated H21 formal assessment from formal observations."""
from __future__ import annotations
import argparse, json, math
from collections import Counter, defaultdict
from pathlib import Path

RULES=Path(__file__).resolve().parents[2]/"standards/technical-condition/jtg-t-h21-2011/1.0.4"
def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))
def grade(score,bounds):
    for item in bounds:
        if score>=item["minimum"] and (score<item["maximum"] or
           (item.get("maximum_inclusive") and score<=item["maximum"])):
            return item["grade"]
    raise AssertionError(("unclassified score",score))
def count_factor(n,points):
    if n==1:return None
    numeric=sorted((p["n"],float(p["t"])) for p in points if isinstance(p["t"],(int,float)))
    if n>=200:return dict(numeric)[200]
    table=dict(numeric)
    if n in table:return table[n]
    lower=max(k for k in table if k<n)
    upper=min(k for k in table if k>n)
    return table[lower]+(table[upper]-table[lower])*(n-lower)/(upper-lower)
def comp_score(points):
    ordered=sorted(points,reverse=True)
    if not ordered:return 100.0,ordered
    if ordered[0]==100:return 0.0,ordered
    remaining=100.0
    for ordinal,dp in enumerate(ordered,1):
        loss=dp if ordinal==1 else dp*remaining/(100.0*math.sqrt(ordinal))
        remaining-=loss
    return remaining,ordered

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("raw",type=Path)
    args=ap.parse_args()
    data=load(args.raw)
    run=data["run"]
    manifest=load(RULES/"manifest.json")
    result=run["result_summary_json"]["result"]
    assert manifest["content_checksum"]==run["rule_package_summary_json"]["content_checksum"]
    assert manifest["package_version"]==result["package_version"]=="1.0.4"
    assert run["rule_package_summary_json"]["rating_tree"]["content_checksum"]==run["input_summary_json"]["rating_tree_content_checksum"]
    assert result["bridge_type_id"]==run["input_summary_json"]["bridge_type_id"]=="h21.bridge_type.beam"
    weights={v["id"]:v for v in load(RULES/"weights.json")["definitions"]}
    deductions={v["id"]:v for v in load(RULES/"deduction-rules.json")["definitions"]}
    bounds=load(RULES/"grade-boundaries.json")["definitions"][0]["grades"]
    bridge_type=next(v for v in load(RULES/"bridge-types.json")["definitions"] if v["id"]==result["bridge_type_id"])
    major=set(bridge_type["major_component_ids"])
    indicators={}
    for catalog in load(RULES/"defect-indicators.json")["definitions"]:
        for item in catalog["indicators"]:
            assert item["id"] not in indicators
            indicators[item["id"]]=(item,set(catalog["applicable_component_ids"]))
    config={}
    for name in ("upper.beam","lower.common","deck.common"):
        block=weights["h21.weight_set."+name]
        for w in block["weights"]:
            config[w["component_id"]]=(block["structure_part"],w["value"])
    structure_weight={w["structure_part"]:w["value"] for w in weights["h21.weight_set.structure.default"]["weights"]}
    factor_points=deductions["h21.calculation.component_count_factor"]["points"]
    errors=[]
    def eq(name,actual,expected,tol=0):
        if isinstance(expected,(int,float)) and not isinstance(expected,bool):
            if actual is None or not math.isclose(float(actual),float(expected),rel_tol=0,abs_tol=tol):
                errors.append({"field":name,"actual":repr(actual),"expected":repr(expected)})
        elif actual!=expected:
            errors.append({"field":name,"actual":repr(actual),"expected":repr(expected)})
    inventory={e["bridge_component_id"]:e for e in data["inventory"]}
    actual_components={c["bridge_component_id"]:c for c in data["components"]}
    eq("inventory_vs_component_set",set(inventory),set(actual_components))
    all_observations=data["observations"]
    scoring=[o for o in all_observations if o["standard_defect_indicator_id"]]
    non_scoring=[o for o in all_observations if not o["standard_defect_indicator_id"]]
    eq("observation_count",len(all_observations),430)
    eq("non_scoring_count",len(non_scoring),54)
    eq("scoring_count",len(scoring),376)
    eq("rating_tree_skip_count",len(run["input_summary_json"]["rating_tree_skips"]),len(non_scoring))
    assert all(o["scale"] is None and o["rating_tree_node_id"] for o in non_scoring)
    assert all(o["scale"] is not None and o["rating_tree_node_id"] for o in scoring)
    groups=defaultdict(dict)
    for o in scoring:
        component=o["bridge_component_id"]
        assert component in inventory
        category=inventory[component]["standard_component_category_id"]
        indicator_id=o["standard_defect_indicator_id"]
        assert indicator_id in indicators,(indicator_id,o["id"])
        indicator,applies=indicators[indicator_id]
        assert category in applies,(indicator_id,category)
        scale=int(o["scale"])
        assert scale in indicator["allowed_scales"]
        previous=groups[component].get(indicator_id,0)
        groups[component][indicator_id]=max(previous,scale)
    # One severity per (actual component, normative indicator): repeated observations use the maximum.
    aggregate_count=sum(map(len,groups.values()))
    eq("deduction_aggregation_count",aggregate_count,354)
    deductions_by_component={}
    score_by_component={}
    for component,entry in inventory.items():
        facts=[]
        for indicator_id,scale in groups[component].items():
            indicator=indicators[indicator_id][0]
            rule=deductions[indicator["deduction_rule_id"]]
            amount=float(rule["points"][str(scale)])
            facts.append((indicator_id,scale,amount,rule["id"]))
        score,ordered=comp_score([f[2] for f in facts])
        deductions_by_component[component]=facts
        score_by_component[component]=score
        saved=actual_components[component]
        eq("component_category:"+component,saved["standard_component_category_id"],entry["standard_component_category_id"])
        eq("component_part:"+component,saved["structure_part"],entry["structure_part"])
        eq("component_score:"+component,saved["score"],score,0.00000051)
        eq("component_deduction:"+component,saved["deduction"],100-score,0.00000051)
        eq("component_ordered_deductions:"+component,saved["result_json"]["ordered_deductions"],ordered)
        expected_facts=Counter((f[0],f[1],f[2],f[3]) for f in facts)
        saved_facts=Counter((v["defect_indicator_id"],v["scale"],v["deduction"],v["deduction_rule_id"])
                            for v in saved["result_json"]["defects"])
        eq("component_defects:"+component,saved_facts,expected_facts)
    result_cats={}
    for part in result["structure_parts"]:
        for cat in part["categories"]:
            result_cats[cat["component_type_id"]]=cat
    by_category=defaultdict(list)
    for component,entry in inventory.items():
        by_category[entry["standard_component_category_id"]].append(score_by_component[component])
    category_scores={}
    for category,scores in by_category.items():
        mean=sum(scores)/len(scores)
        minimum=min(scores)
        t=count_factor(len(scores),factor_points)
        passthrough=category in major and 0<=minimum<40
        expected=minimum if passthrough else (mean if t is None else mean-(100-minimum)/t)
        category_scores[category]=expected
        saved=result_cats[category]
        eq("category_count:"+category,len(saved["components"]),len(scores))
        eq("category_mean:"+category,saved["mean_component_score"],mean,1e-8)
        eq("category_minimum:"+category,saved["minimum_component_score"],minimum,1e-8)
        eq("category_factor:"+category,saved["component_count_factor"],t,1e-8)
        eq("category_passthrough:"+category,saved["low_score_passthrough"],passthrough)
        eq("category_major:"+category,saved["major"],category in major)
        eq("category_score:"+category,saved["score"],expected,1e-8)
        eq("category_grade:"+category,saved["grade"],grade(expected,bounds))
    eq("category_set",set(result_cats),set(by_category))
    part_scores={}
    for part in ("superstructure","substructure","deck_system"):
        categories=[c for c in category_scores if config[c][0]==part]
        denom=sum(config[c][1] for c in categories)
        part_score=sum(category_scores[c]*config[c][1]/denom for c in categories)
        part_scores[part]=part_score
        saved=next(p for p in result["structure_parts"] if p["structure_part"]==part)
        eq("part_score:"+part,saved["score"],part_score,1e-8)
        eq("part_grade:"+part,saved["grade"],grade(part_score,bounds))
        eq("part_weight:"+part,saved["overall_weight"],structure_weight[part],1e-10)
        for cat in saved["categories"]:
            category=cat["component_type_id"]
            eq("configured_weight:"+category,cat["configured_weight"],config[category][1],1e-10)
            eq("effective_weight:"+category,cat["effective_weight"],config[category][1]/denom,1e-10)
    overall=sum(part_scores[p]*structure_weight[p] for p in part_scores)
    overall_grade=grade(overall,bounds)
    part_grades={part:grade(value,bounds) for part,value in part_scores.items()}
    combination_triggered=(part_grades=={"superstructure":3,"substructure":3,"deck_system":4}
                           and 40.0<=overall<60.0)
    eq("grade3_combination_triggered",combination_triggered,False)
    eq("overall_score",result["overall_score"],overall,1e-8)
    eq("calculated_grade",result["calculated_grade"],overall_grade)
    eq("final_grade",result["final_grade"],overall_grade)
    eq("annual_score",data["year"]["overall_score"],round(overall,2),0.005)
    eq("annual_grade",data["year"]["overall_grade"],str(overall_grade)+"类")
    eq("triggered_controls",data["controls"],[])
    eq("reported_controls",result["triggered_controls"],[])
    part_rows={(p["result_level"],p["result_key"]):p for p in data["parts"]}
    eq("part_projection_count",len(data["parts"]),len(category_scores)+4)
    for category,value in category_scores.items():
        row=next(p for p in data["parts"] if p["result_level"]=="部件" and p["standard_component_category_id"]==category)
        eq("category_projection:"+category,row["score"],value,0.00000051)
    for part,value in part_scores.items():
        row=next(p for p in data["parts"] if p["result_level"]=="结构" and p["structure_part"]==part)
        eq("part_projection:"+part,row["score"],value,0.00000051)
    eq("overall_projection",part_rows[("全桥","overall")]["score"],overall,0.00000051)
    stages=Counter(t["trace_stage"] for t in data["traces"])
    eq("trace_total",len(data["traces"]),len(inventory)+aggregate_count+len(category_scores)+3+1+1+len(non_scoring))
    eq("trace_stage_set",set(stages),{"component_score","defect_deduction",
       "component_category_score","structure_part_score","overall_score","grade","rating_tree_filter"})
    eq("trace_component",stages["component_score"],len(inventory))
    eq("trace_deduction",stages["defect_deduction"],aggregate_count)
    eq("trace_category",stages["component_category_score"],len(category_scores))
    eq("trace_structure",stages["structure_part_score"],3)
    eq("trace_non_scoring",stages["rating_tree_filter"],len(non_scoring))
    eq("trace_overall",stages["overall_score"],1)
    eq("trace_grade",stages["grade"],1)
    assert all(t["rule_id"]=="rating_tree_non_scoring" for t in data["traces"] if t["trace_stage"]=="rating_tree_filter")
    by_deduction_trace={t["target_key"]:t for t in data["traces"] if t["trace_stage"]=="defect_deduction"}
    eq("trace_deduction_ids",set(by_deduction_trace),
       {component+":"+fact[0] for component,facts in deductions_by_component.items() for fact in facts})
    for component,facts in deductions_by_component.items():
        for indicator_id,scale,amount,rule_id in facts:
            t=by_deduction_trace[component+":"+indicator_id]
            eq("deduction_trace_rule:"+component+":"+indicator_id,t["rule_id"],rule_id)
            eq("deduction_trace_scale:"+component+":"+indicator_id,t["input_json"]["scale"],scale)
            eq("deduction_trace_value:"+component+":"+indicator_id,t["output_json"]["deduction"],amount,1e-10)
    source_groups=defaultdict(list)
    for o in all_observations:
        origin=o["source_raw_cells_json"].get("range_split_origin")
        if origin:source_groups[origin["source_candidate_id"]].append(o)
    eq("expanded_sources",{k:len(v) for k,v in source_groups.items()},
       {"source_defect_0317":25,"source_defect_0328":25,"source_defect_0162":2})
    for candidate in ("source_defect_0317","source_defect_0328"):
        eq("non_scoring_range:"+candidate,
           sum(bool(o["standard_defect_indicator_id"]) for o in source_groups[candidate]),0)
    side=source_groups["source_defect_0162"]
    eq("side_distinct_targets",len({o["bridge_component_id"] for o in side}),2)
    eq("side_scored_targets",sum(bool(o["standard_defect_indicator_id"]) for o in side),2)
    side_results=[]
    for o in side:
        component=o["bridge_component_id"]
        side_results.append({"component_number":inventory[component]["component_number"],
            "component_score":score_by_component[component],"indicator_id":o["standard_defect_indicator_id"],"scale":o["scale"]})
    right=next(o for o in side if inventory[o["bridge_component_id"]]["component_number"]=="右侧栏杆")
    right_id=right["bridge_component_id"]
    remaining=[o for o in scoring if o["bridge_component_id"]==right_id and o["id"]!=right["id"]]
    remaining_scales={}
    for o in remaining:
        key=o["standard_defect_indicator_id"]
        remaining_scales[key]=max(remaining_scales.get(key,0),int(o["scale"]))
    right_deductions=[]
    for indicator_id,scale in remaining_scales.items():
        rule_id=indicators[indicator_id][0]["deduction_rule_id"]
        right_deductions.append(float(deductions[rule_id]["points"][str(scale)]))
    right_without,_=comp_score(right_deductions)
    railing="h21.component.deck.railing"
    rail_scores=[score_by_component[c] for c,e in inventory.items()
                 if e["standard_component_category_id"]==railing]
    without_scores=rail_scores.copy()
    without_scores.remove(score_by_component[right_id])
    without_scores.append(right_without)
    rail_t=count_factor(len(without_scores),factor_points)
    rail_without=sum(without_scores)/len(without_scores)-(100-min(without_scores))/rail_t
    deck_categories=[c for c in category_scores if config[c][0]=="deck_system"]
    deck_denom=sum(config[c][1] for c in deck_categories)
    deck_without=part_scores["deck_system"]+(rail_without-category_scores[railing])*config[railing][1]/deck_denom
    overall_without=overall+(deck_without-part_scores["deck_system"])*structure_weight["deck_system"]
    eq("right_side_counterfactual_current_score",score_by_component[right_id],60,0.00000051)
    report={
      "passed":not errors,"source":"formal observations + confirmed inventory + versioned H21 rule package",
      "rule_checksum":manifest["content_checksum"],"observations":len(all_observations),
      "scoring_observations":len(scoring),"non_scoring_observations":len(non_scoring),
      "unique_component_indicator_pairs":aggregate_count,"components":len(inventory),
      "categories":len(category_scores),"part_scores":part_scores,
      "overall_score_independent":overall,"overall_score_stored":result["overall_score"],
      "overall_grade":overall_grade,"trace_stage_counts":dict(stages),
      "range_effect":"50 expanded observations retain evidence; both source defects are non-scoring and contribute zero deduction",
      "side_targets":side_results,
      "right_side_omission_sensitivity":{
        "right_component_score_without_this_source":right_without,
        "railing_category_score_without_this_source":rail_without,
        "deck_score_without_this_source":deck_without,
        "overall_score_without_this_source":overall_without,
        "overall_score_difference":overall_without-overall,
        "purpose":"diagnostic counterfactual only; no database mutation"
      },"errors":errors[:100],"error_count":len(errors)}
    out=args.raw.parent/"source-assessment-audit.json"
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    print(json.dumps(report,ensure_ascii=False,indent=2))
    if errors:raise SystemExit(1)
if __name__=="__main__":main()
