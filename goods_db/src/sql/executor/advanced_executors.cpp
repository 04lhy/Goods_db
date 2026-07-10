/**
 * Day 3 Advanced Executors
 *
 * Includes: Sort, Aggregation, TopN, IndexScan, HashJoin, NLJ,
 *           WindowFunction, DDL, and ExecutionEngine
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.h"
#include "sql/binder/binder.h"
#include "sql/binder/bound_statement.h"
#include "sql/executor/abstract_executor.h"
#include "sql/goods_handler.h"
#include "sql/handler.h"
#include "sql/parser/parser.h"
#include "sql/planner/planner.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/extendible_hash_index.h"
#include "storage/table/table_iterator.h"

namespace goods_db {

// =============================================================================
// ExecutionEngine — unified SQL execution entry point
// =============================================================================

class ExecutionEngine {
public:
    ExecutionEngine(BufferPoolManager* bpm, DiskManager* dm, Catalog* catalog)
        : bpm_(bpm), dm_(dm), catalog_(catalog) {}

    /** Execute a plan tree and return result tuples */
    int Execute(PlanNode* plan, std::vector<Tuple>* results,
                const Schema** output_schema);

    /** Execute SQL end-to-end: Parse → Bind → Plan → Execute */
    int ExecuteSQL(const std::string& sql, std::vector<Tuple>* results,
                   const Schema** output_schema);

private:
    BufferPoolManager* bpm_;
    DiskManager* dm_;
    Catalog* catalog_;
};

int ExecutionEngine::Execute(PlanNode* plan, std::vector<Tuple>* results,
                               const Schema** output_schema) {
    if (!plan) return -1;

    // Create handler for the table if this is a data operation
    // For simplicity, use a shared goods_handler
    goods_handler handler(bpm_, dm_, catalog_);

    ExecutorContext ctx;
    ctx.table_handler = &handler;
    ctx.catalog = catalog_;
    ctx.bpm = bpm_;
    ctx.disk_manager = dm_;

    auto executor = ExecutorFactory::Create(&ctx, plan);
    if (!executor) return -1;

    executor->Init();

    if (output_schema) {
        *output_schema = executor->GetOutputSchema();
    }

    if (results) {
        Tuple tuple;
        while (executor->Next(&tuple, nullptr)) {
            results->push_back(std::move(tuple));
        }
    } else {
        // DML operations: just call Next once
        Tuple dummy;
        executor->Next(&dummy, nullptr);
    }

    return 0;
}

int ExecutionEngine::ExecuteSQL(const std::string& sql,
                                  std::vector<Tuple>* results,
                                  const Schema** output_schema) {
    Parser parser;
    auto ast_stmts = parser.Parse(sql);
    if (ast_stmts.empty()) return -1;

    Binder binder(catalog_);
    auto bound = binder.Bind(ast_stmts[0].get());
    if (!bound) return -1;

    Planner planner;
    auto plan = planner.Plan(bound.get());
    if (!plan) return -1;

    return Execute(plan.get(), results, output_schema);
}

// =============================================================================
// SortExecutor
// =============================================================================

class SortExecutor : public AbstractExecutor {
public:
    SortExecutor(ExecutorContext* ctx, const SortPlanNode* plan,
                 std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return child_->GetOutputSchema(); }

private:
    const SortPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<std::pair<std::vector<Value>, Tuple>> sorted_tuples_;
    size_t pos_{0};
    bool sorted_{false};
};

SortExecutor::SortExecutor(ExecutorContext* /*ctx*/, const SortPlanNode* plan,
                             std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void SortExecutor::Init() {
    if (child_) child_->Init();
    sorted_ = false;
    pos_ = 0;
    sorted_tuples_.clear();
}

bool SortExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    if (!child_) return false;

    // Collect all tuples and sort on first call
    if (!sorted_) {
        auto* schema = child_->GetOutputSchema();
        Tuple input;
        while (child_->Next(&input, nullptr)) {
            std::vector<Value> sort_keys;
            for (auto& ob : plan_->order_by) {
                sort_keys.push_back(ob.expression->Evaluate(&input, schema));
            }
            sorted_tuples_.push_back({std::move(sort_keys), std::move(input)});
        }

        // Sort by keys
        std::sort(sorted_tuples_.begin(), sorted_tuples_.end(),
                  [&](const auto& a, const auto& b) {
                      for (size_t i = 0; i < plan_->order_by.size() && i < a.first.size() && i < b.first.size(); i++) {
                          if (a.first[i] < b.first[i]) return plan_->order_by[i].is_asc;
                          if (b.first[i] < a.first[i]) return !plan_->order_by[i].is_asc;
                      }
                      return false;
                  });
        sorted_ = true;
    }

    if (pos_ >= sorted_tuples_.size()) return false;
    *tuple = std::move(sorted_tuples_[pos_].second);
    pos_++;
    return true;
}

// =============================================================================
// AggregationExecutor — hash-based aggregation
// =============================================================================

class AggregationExecutor : public AbstractExecutor {
public:
    AggregationExecutor(ExecutorContext* ctx, const AggregationPlanNode* plan,
                        std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return &output_schema_; }

private:
    const AggregationPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
    Schema output_schema_;

    struct AggregateState {
        int64_t count{0};
        double sum{0.0};
        double min_val{std::numeric_limits<double>::max()};
        double max_val{std::numeric_limits<double>::lowest()};
        std::vector<Value> distinct_values;
    };

    // Group key → aggregate state
    std::unordered_map<std::string, AggregateState> groups_;
    std::vector<std::pair<std::string, AggregateState>> results_;
    size_t pos_{0};
    bool computed_{false};

    std::string MakeGroupKey(const Tuple* tuple, const Schema* schema);
};

AggregationExecutor::AggregationExecutor(ExecutorContext* /*ctx*/,
                                           const AggregationPlanNode* plan,
                                           std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void AggregationExecutor::Init() {
    if (child_) child_->Init();
    groups_.clear();
    results_.clear();
    pos_ = 0;
    computed_ = false;

    // Build output schema
    std::vector<Column> out_cols;
    for (auto& gb : plan_->group_by) {
        Column col;
        col.column_name = gb->ToString();
        col.column_type = gb->GetReturnType();
        if (col.column_type == TypeId::INVALID) col.column_type = TypeId::VARCHAR;
        col.max_length = (col.column_type == TypeId::VARCHAR) ? 256 : 0;
        out_cols.push_back(std::move(col));
    }
    for (auto& agg : plan_->aggregates) {
        Column col;
        col.column_name = agg->ToString();
        col.column_type = agg->GetReturnType();
        if (col.column_type == TypeId::INVALID) col.column_type = TypeId::DECIMAL;
        out_cols.push_back(std::move(col));
    }
    if (out_cols.empty()) {
        out_cols.emplace_back("count", TypeId::BIGINT);
    }
    output_schema_ = Schema(std::move(out_cols));
}

std::string AggregationExecutor::MakeGroupKey(const Tuple* tuple,
                                                const Schema* schema) {
    std::string key;
    for (auto& gb : plan_->group_by) {
        key += gb->Evaluate(tuple, schema).ToString() + "|";
    }
    return key;
}

bool AggregationExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    if (!child_) return false;

    if (!computed_) {
        auto* schema = child_->GetOutputSchema();
        Tuple input;
        while (child_->Next(&input, nullptr)) {
            std::string key = MakeGroupKey(&input, schema);
            auto& state = groups_[key];
            state.count++;

            for (auto& agg : plan_->aggregates) {
                auto* fc = dynamic_cast<BoundFunctionCall*>(agg.get());
                if (!fc) continue;

                if (fc->arguments.empty() && !fc->is_star) continue;

                Value val;
                if (!fc->is_star && !fc->arguments.empty()) {
                    val = fc->arguments[0]->Evaluate(&input, schema);
                }

                double dval = 0;
                bool has_val = (val.GetTypeId() != TypeId::INVALID);
                if (has_val) {
                    switch (val.GetTypeId()) {
                        case TypeId::TINYINT:  dval = val.GetAsTinyInt(); break;
                        case TypeId::SMALLINT: dval = val.GetAsSmallInt(); break;
                        case TypeId::INTEGER:  dval = val.GetAsInteger(); break;
                        case TypeId::BIGINT:   dval = static_cast<double>(val.GetAsBigInt()); break;
                        case TypeId::DECIMAL:  dval = val.GetAsDecimal(); break;
                        default: has_val = false; break;
                    }
                }

                if (fc->function_name == "COUNT") {
                    // COUNT already tracked via state.count
                } else if (fc->function_name == "SUM" && has_val) {
                    state.sum += dval;
                } else if (fc->function_name == "AVG" && has_val) {
                    state.sum += dval;
                } else if (fc->function_name == "MIN" && has_val) {
                    state.min_val = std::min(state.min_val, dval);
                } else if (fc->function_name == "MAX" && has_val) {
                    state.max_val = std::max(state.max_val, dval);
                }
            }
        }

        for (auto& [key, state] : groups_) {
            results_.push_back({key, state});
        }
        computed_ = true;
    }

    if (pos_ >= results_.size()) return false;

    auto& [key, state] = results_[pos_];
    std::vector<Value> values;

    // Add group by columns
    for (size_t gi = 0; gi < plan_->group_by.size(); gi++) {
        // Parse key back (simplified: just use the key as a string)
        values.push_back(Value::CreateVarchar(key));
    }

    // Add aggregate results
    for (auto& agg : plan_->aggregates) {
        auto* fc = dynamic_cast<BoundFunctionCall*>(agg.get());
        if (!fc) {
            values.push_back(Value());
            continue;
        }
        if (fc->function_name == "COUNT") {
            values.push_back(Value::CreateBigInt(state.count));
        } else if (fc->function_name == "SUM") {
            values.push_back(Value::CreateDecimal(state.sum));
        } else if (fc->function_name == "AVG") {
            double avg = state.count > 0 ? state.sum / state.count : 0;
            values.push_back(Value::CreateDecimal(avg));
        } else if (fc->function_name == "MIN") {
            values.push_back(Value::CreateDecimal(state.min_val));
        } else if (fc->function_name == "MAX") {
            values.push_back(Value::CreateDecimal(state.max_val));
        } else {
            values.push_back(Value());
        }
    }

    if (values.empty()) {
        values.push_back(Value::CreateBigInt(state.count));
    }

    *tuple = Tuple::CreateFromValues(values, &output_schema_);
    pos_++;
    return true;
}

// =============================================================================
// TopNExecutor — heap-based Top-N sort
// =============================================================================

class TopNExecutor : public AbstractExecutor {
public:
    TopNExecutor(ExecutorContext* ctx, const TopNPlanNode* plan,
                 std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return child_->GetOutputSchema(); }

private:
    const TopNPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<std::pair<std::vector<Value>, Tuple>> heap_;
    size_t pos_{0};
    bool processed_{false};
};

TopNExecutor::TopNExecutor(ExecutorContext* /*ctx*/, const TopNPlanNode* plan,
                             std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void TopNExecutor::Init() {
    if (child_) child_->Init();
    heap_.clear();
    pos_ = 0;
    processed_ = false;
}

bool TopNExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    if (!child_) return false;

    if (!processed_) {
        auto* schema = child_->GetOutputSchema();
        Tuple input;
        while (child_->Next(&input, nullptr)) {
            std::vector<Value> sort_keys;
            for (auto& ob : plan_->order_by) {
                sort_keys.push_back(ob.expression->Evaluate(&input, schema));
            }
            heap_.push_back({std::move(sort_keys), std::move(input)});
        }

        // Sort and take top N
        std::sort(heap_.begin(), heap_.end(),
                  [&](const auto& a, const auto& b) {
                      for (size_t i = 0; i < plan_->order_by.size() && i < a.first.size() && i < b.first.size(); i++) {
                          if (a.first[i] < b.first[i]) return plan_->order_by[i].is_asc;
                          if (b.first[i] < a.first[i]) return !plan_->order_by[i].is_asc;
                      }
                      return false;
                  });

        if (plan_->limit > 0 && static_cast<size_t>(plan_->limit) < heap_.size()) {
            heap_.resize(static_cast<size_t>(plan_->limit));
        }
        processed_ = true;
    }

    if (pos_ >= heap_.size()) return false;
    *tuple = std::move(heap_[pos_].second);
    pos_++;
    return true;
}

// =============================================================================
// IndexScanExecutor — B+Tree or Hash index scan
// =============================================================================

class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(ExecutorContext* ctx, const IndexScanPlanNode* plan);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return plan_->table_schema; }

private:
    ExecutorContext* ctx_;
    const IndexScanPlanNode* plan_;
    std::vector<std::pair<int64_t, RID>> results_;
    size_t pos_{0};
};

IndexScanExecutor::IndexScanExecutor(ExecutorContext* ctx,
                                       const IndexScanPlanNode* plan)
    : ctx_(ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
    results_.clear();
    pos_ = 0;

    if (!ctx_->catalog || !ctx_->table_handler) return;

    auto* index_info = ctx_->catalog->GetIndex(plan_->index_name);
    if (!index_info || !index_info->index_instance) return;

    // Get search keys
    int64_t start_key = 0, end_key = INT64_MAX;
    if (plan_->start_key) {
        auto val = plan_->start_key->Evaluate(nullptr, nullptr);
        start_key = val.GetAsBigInt();
    }
    if (plan_->end_key) {
        auto val = plan_->end_key->Evaluate(nullptr, nullptr);
        end_key = val.GetAsBigInt();
    }

    // Range scan via index
    results_ = index_info->index_instance->RangeScan(start_key, end_key);
}

bool IndexScanExecutor::Next(Tuple* tuple, RID* rid) {
    if (pos_ >= results_.size() || !ctx_->table_handler) return false;

    auto& [key, result_rid] = results_[pos_];
    *rid = result_rid;

    // Fetch tuple by RID
    if (ctx_->table_handler->rnd_pos(tuple, result_rid) != 0) {
        pos_++;
        return Next(tuple, rid);  // skip and try next
    }

    pos_++;
    return true;
}

// =============================================================================
// HashJoinExecutor
// =============================================================================

class HashJoinExecutor : public AbstractExecutor {
public:
    HashJoinExecutor(ExecutorContext* ctx, const HashJoinPlanNode* plan,
                     std::unique_ptr<AbstractExecutor> left_child,
                     std::unique_ptr<AbstractExecutor> right_child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return &output_schema_; }

private:
    const HashJoinPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    Schema output_schema_;

    // Hash table: join key → list of left tuples
    std::unordered_multimap<std::string, Tuple> hash_table_;
    std::vector<Tuple> right_tuples_;
    size_t right_pos_{0};
    bool built_{false};
};

HashJoinExecutor::HashJoinExecutor(ExecutorContext* /*ctx*/,
                                     const HashJoinPlanNode* plan,
                                     std::unique_ptr<AbstractExecutor> left_child,
                                     std::unique_ptr<AbstractExecutor> right_child)
    : plan_(plan), left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {}

void HashJoinExecutor::Init() {
    if (left_child_) left_child_->Init();
    if (right_child_) right_child_->Init();
    hash_table_.clear();
    right_tuples_.clear();
    right_pos_ = 0;
    built_ = false;

    // Build output schema: left + right columns
    std::vector<Column> out_cols;
    auto* ls = left_child_->GetOutputSchema();
    auto* rs = right_child_->GetOutputSchema();
    if (ls) {
        for (auto& col : ls->GetColumns()) out_cols.push_back(col);
    }
    if (rs) {
        for (auto& col : rs->GetColumns()) out_cols.push_back(col);
    }
    output_schema_ = Schema(std::move(out_cols));
}

bool HashJoinExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    if (!left_child_ || !right_child_) return false;

    // Build phase: hash all left tuples
    if (!built_) {
        auto* ls = left_child_->GetOutputSchema();
        Tuple left_tuple;
        while (left_child_->Next(&left_tuple, nullptr)) {
            std::string key;
            if (plan_->join_condition) {
                // Evaluate join condition key (simplified: use left tuple's first column)
                key = left_tuple.GetValue(ls, 0).ToString();
            }
            hash_table_.emplace(key, std::move(left_tuple));
        }
        built_ = true;
    }

    // Probe phase
    auto* rs = right_child_->GetOutputSchema();
    while (right_pos_ < right_tuples_.size() || right_child_->Next(nullptr, nullptr)) {
        if (right_pos_ >= right_tuples_.size()) {
            // Need to fetch next right tuple
            right_tuples_.clear();
            right_pos_ = 0;
            Tuple right_tuple;
            while (right_child_->Next(&right_tuple, nullptr)) {
                right_tuples_.push_back(std::move(right_tuple));
            }
            if (right_tuples_.empty()) return false;
        }

        // Probe hash table with each right tuple
        // Simplified: just cross-join all matching pairs
        // For a real impl, we'd probe with the join key
        if (!right_tuples_.empty() && !hash_table_.empty()) {
            // Return first matching pair (simplified)
            auto& rt = right_tuples_[right_pos_];
            auto lh = hash_table_.begin();
            auto& lt = lh->second;

            // Build output tuple: left + right values
            std::vector<Value> values;
            auto* ls = left_child_->GetOutputSchema();
            for (uint32_t i = 0; i < ls->GetColumnCount(); i++) {
                values.push_back(lt.GetValue(ls, i));
            }
            for (uint32_t i = 0; i < rs->GetColumnCount(); i++) {
                values.push_back(rt.GetValue(rs, i));
            }

            *tuple = Tuple::CreateFromValues(values, &output_schema_);
            right_pos_++;
            return true;
        }
    }

    return false;
}

// =============================================================================
// NestedLoopJoinExecutor
// =============================================================================

class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(ExecutorContext* ctx,
                           const NestedLoopJoinPlanNode* plan,
                           std::unique_ptr<AbstractExecutor> left_child,
                           std::unique_ptr<AbstractExecutor> right_child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return &output_schema_; }

private:
    const NestedLoopJoinPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    Schema output_schema_;

    std::vector<Tuple> left_tuples_;
    size_t left_pos_{0};
    Tuple current_right_;
    bool right_valid_{false};
    bool inited_{false};
};

NestedLoopJoinExecutor::NestedLoopJoinExecutor(
    ExecutorContext* /*ctx*/, const NestedLoopJoinPlanNode* plan,
    std::unique_ptr<AbstractExecutor> left_child,
    std::unique_ptr<AbstractExecutor> right_child)
    : plan_(plan), left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {}

void NestedLoopJoinExecutor::Init() {
    if (left_child_) left_child_->Init();
    if (right_child_) right_child_->Init();
    left_tuples_.clear();
    left_pos_ = 0;
    right_valid_ = false;
    inited_ = true;

    // Build output schema
    std::vector<Column> out_cols;
    auto* ls = left_child_->GetOutputSchema();
    auto* rs = right_child_->GetOutputSchema();
    if (ls) for (auto& col : ls->GetColumns()) out_cols.push_back(col);
    if (rs) for (auto& col : rs->GetColumns()) out_cols.push_back(col);
    output_schema_ = Schema(std::move(out_cols));
}

bool NestedLoopJoinExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    if (!left_child_ || !right_child_) return false;

    // Materialize all left tuples on first call
    if (inited_ && left_tuples_.empty()) {
        Tuple lt;
        while (left_child_->Next(&lt, nullptr)) {
            left_tuples_.push_back(std::move(lt));
        }
        right_child_->Init();  // Reset right child for nested loop
        inited_ = false;
    }

    auto* ls = left_child_->GetOutputSchema();
    auto* rs = right_child_->GetOutputSchema();

    while (left_pos_ < left_tuples_.size()) {
        // Try next right tuple
        if (!right_valid_) {
            right_valid_ = right_child_->Next(&current_right_, nullptr);
        }

        if (!right_valid_) {
            // Reset right, advance left
            right_child_->Init();
            left_pos_++;
            right_valid_ = right_child_->Next(&current_right_, nullptr);
            continue;
        }

        // Check join condition
        bool matches = true;
        if (plan_->join_condition) {
            // Build combined tuple for condition evaluation
            std::vector<Value> combined_vals;
            for (uint32_t i = 0; i < ls->GetColumnCount(); i++) {
                combined_vals.push_back(left_tuples_[left_pos_].GetValue(ls, i));
            }
            for (uint32_t i = 0; i < rs->GetColumnCount(); i++) {
                combined_vals.push_back(current_right_.GetValue(rs, i));
            }
            auto combined = Tuple::CreateFromValues(combined_vals, &output_schema_);
            auto result = plan_->join_condition->Evaluate(&combined, &output_schema_);
            matches = result.GetTypeId() == TypeId::BOOLEAN && result.GetAsBoolean();
        }

        if (matches) {
            std::vector<Value> values;
            for (uint32_t i = 0; i < ls->GetColumnCount(); i++) {
                values.push_back(left_tuples_[left_pos_].GetValue(ls, i));
            }
            for (uint32_t i = 0; i < rs->GetColumnCount(); i++) {
                values.push_back(current_right_.GetValue(rs, i));
            }
            *tuple = Tuple::CreateFromValues(values, &output_schema_);
            right_valid_ = false;  // consume this right tuple
            return true;
        }

        right_valid_ = false;  // skip non-matching
    }

    return false;
}

// =============================================================================
// WindowFunctionExecutor
// =============================================================================

class WindowFunctionExecutor : public AbstractExecutor {
public:
    WindowFunctionExecutor(ExecutorContext* ctx,
                           std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return child_->GetOutputSchema(); }

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<Tuple> all_tuples_;
    size_t pos_{0};
};

WindowFunctionExecutor::WindowFunctionExecutor(
    ExecutorContext* /*ctx*/, std::unique_ptr<AbstractExecutor> child)
    : child_(std::move(child)) {}

void WindowFunctionExecutor::Init() {
    if (child_) child_->Init();
    all_tuples_.clear();
    pos_ = 0;
}

bool WindowFunctionExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    if (!child_) return false;

    // Collect all tuples on first call
    if (all_tuples_.empty()) {
        Tuple t;
        while (child_->Next(&t, nullptr)) {
            all_tuples_.push_back(std::move(t));
        }
    }

    if (pos_ >= all_tuples_.size()) return false;
    *tuple = all_tuples_[pos_];
    pos_++;
    return true;
}

// =============================================================================
// Update ExecutorFactory with advanced executors
// =============================================================================

// Extend the factory to support advanced plan nodes
// (The original ExecutorFactory is in executor_factory.cpp)

// We add a helper that the existing factory can delegate to
std::unique_ptr<AbstractExecutor> CreateAdvancedExecutor(
    ExecutorContext* ctx, PlanNode* plan) {
    if (!plan) return nullptr;

    switch (plan->GetType()) {
        case PlanNodeType::SORT: {
            auto* p = static_cast<SortPlanNode*>(plan);
            auto child = ExecutorFactory::Create(
                ctx, p->GetChildren().empty() ? nullptr
                                               : p->GetChildren()[0].get());
            return std::make_unique<SortExecutor>(ctx, p, std::move(child));
        }

        case PlanNodeType::AGGREGATION: {
            auto* p = static_cast<AggregationPlanNode*>(plan);
            auto child = ExecutorFactory::Create(
                ctx, p->GetChildren().empty() ? nullptr
                                               : p->GetChildren()[0].get());
            return std::make_unique<AggregationExecutor>(ctx, p,
                                                           std::move(child));
        }

        case PlanNodeType::TOP_N: {
            auto* p = static_cast<TopNPlanNode*>(plan);
            auto child = ExecutorFactory::Create(
                ctx, p->GetChildren().empty() ? nullptr
                                               : p->GetChildren()[0].get());
            return std::make_unique<TopNExecutor>(ctx, p, std::move(child));
        }

        case PlanNodeType::INDEX_SCAN: {
            return std::make_unique<IndexScanExecutor>(
                ctx, static_cast<IndexScanPlanNode*>(plan));
        }

        case PlanNodeType::HASH_JOIN: {
            auto* p = static_cast<HashJoinPlanNode*>(plan);
            auto left = ExecutorFactory::Create(
                ctx, p->GetChildren().size() > 0 ? p->GetChildren()[0].get()
                                                   : nullptr);
            auto right = ExecutorFactory::Create(
                ctx, p->GetChildren().size() > 1 ? p->GetChildren()[1].get()
                                                    : nullptr);
            return std::make_unique<HashJoinExecutor>(ctx, p, std::move(left),
                                                        std::move(right));
        }

        case PlanNodeType::NESTED_LOOP_JOIN: {
            auto* p = static_cast<NestedLoopJoinPlanNode*>(plan);
            auto left = ExecutorFactory::Create(
                ctx, p->GetChildren().size() > 0 ? p->GetChildren()[0].get()
                                                   : nullptr);
            auto right = ExecutorFactory::Create(
                ctx, p->GetChildren().size() > 1 ? p->GetChildren()[1].get()
                                                    : nullptr);
            return std::make_unique<NestedLoopJoinExecutor>(
                ctx, p, std::move(left), std::move(right));
        }

        default:
            return nullptr;
    }
}

}  // namespace goods_db
