#include "sql/executor/abstract_executor.h"

#include <stdexcept>

#include "catalog/catalog.h"
#include "sql/handler.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/extendible_hash_index.h"
#include "storage/table/table_iterator.h"

// Forward declaration for advanced executor creation (implemented in advanced_executors.cpp)
namespace goods_db {
std::unique_ptr<AbstractExecutor> CreateAdvancedExecutor(
    ExecutorContext* ctx, PlanNode* plan);
}

namespace goods_db {

// =============================================================================
// SeqScanExecutor
// =============================================================================

SeqScanExecutor::SeqScanExecutor(ExecutorContext* ctx,
                                   const SeqScanPlanNode* plan)
    : ctx_(ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
    if (ctx_->table_handler) {
        // Open the table before scanning (the handler is created fresh each query)
        ctx_->table_handler->close();
        ctx_->table_handler->open(plan_->table_name.c_str());
        ctx_->table_handler->rnd_init(true);
    }
    scan_started_ = true;
}

bool SeqScanExecutor::Next(Tuple* tuple, RID* rid) {
    if (!ctx_->table_handler || !scan_started_) return false;

    int ret = ctx_->table_handler->rnd_next(tuple);
    if (ret == 0 && rid) {
        *rid = tuple->GetRid();
    }
    return ret == 0;
}

// =============================================================================
// FilterExecutor
// =============================================================================

FilterExecutor::FilterExecutor(ExecutorContext* /*ctx*/,
                                 const FilterPlanNode* plan,
                                 std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void FilterExecutor::Init() {
    if (child_) child_->Init();
}

bool FilterExecutor::Next(Tuple* tuple, RID* rid) {
    if (!child_ || !plan_->predicate) return false;

    auto* schema = child_->GetOutputSchema();
    while (child_->Next(tuple, rid)) {
        auto result = plan_->predicate->Evaluate(tuple, schema);
        if (result.GetTypeId() == TypeId::BOOLEAN && result.GetAsBoolean()) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// ProjectionExecutor
// =============================================================================

ProjectionExecutor::ProjectionExecutor(ExecutorContext* /*ctx*/,
                                         const ProjectionPlanNode* plan,
                                         std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void ProjectionExecutor::Init() {
    if (child_) child_->Init();
    produced_ = false;

    // Build output schema from expressions and store it in the plan node
    // (so it outlives this executor — the plan node is owned by the caller)
    std::vector<Column> out_cols;
    for (auto& expr : plan_->expressions) {
        Column col;
        col.column_name = expr->alias.empty() ? expr->ToString() : expr->alias;
        col.column_type = expr->GetReturnType();
        if (col.column_type == TypeId::INVALID) {
            col.column_type = TypeId::VARCHAR;
        }
        col.max_length = (col.column_type == TypeId::VARCHAR) ? 256 : 0;
        out_cols.push_back(std::move(col));
    }
    // Store schema on plan node — GetOutputSchema() points to this
    const_cast<ProjectionPlanNode*>(plan_)->SetOutputSchema(Schema(std::move(out_cols)));
}

bool ProjectionExecutor::Next(Tuple* tuple, RID* /*rid*/) {
    const Schema* out_schema = &plan_->GetOutputSchema();

    // If no child (e.g. SELECT 1 without FROM), produce one row from constants
    if (!child_) {
        if (produced_) return false;
        produced_ = true;
        // Evaluate expressions against nullptr/empty schema — constants work, col refs become NULL
        std::vector<Value> values;
        Tuple dummy;
        for (auto& expr : plan_->expressions) {
            values.push_back(expr->Evaluate(&dummy, nullptr));
        }
        *tuple = Tuple::CreateFromValues(values, out_schema);
        return true;
    }

    Tuple input_tuple;
    auto* input_schema = child_->GetOutputSchema();
    if (!child_->Next(&input_tuple, nullptr)) return false;

    // Evaluate each expression against the input tuple
    std::vector<Value> values;
    for (auto& expr : plan_->expressions) {
        auto val = expr->Evaluate(&input_tuple, input_schema);
        values.push_back(std::move(val));
    }

    *tuple = Tuple::CreateFromValues(values, out_schema);
    return true;
}

// =============================================================================
// LimitExecutor
// =============================================================================

LimitExecutor::LimitExecutor(ExecutorContext* /*ctx*/,
                               const LimitPlanNode* plan,
                               std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void LimitExecutor::Init() {
    if (child_) child_->Init();
    returned_ = 0;
    skipped_ = 0;
}

bool LimitExecutor::Next(Tuple* tuple, RID* rid) {
    if (!child_) return false;

    // Skip offset rows
    while (skipped_ < plan_->offset) {
        Tuple dummy;
        if (!child_->Next(&dummy, nullptr)) return false;
        skipped_++;
    }

    // Check limit
    if (plan_->limit >= 0 && returned_ >= plan_->limit) return false;

    bool has_next = child_->Next(tuple, rid);
    if (has_next) returned_++;
    return has_next;
}

// =============================================================================
// InsertExecutor
// =============================================================================

InsertExecutor::InsertExecutor(ExecutorContext* ctx,
                                 const InsertPlanNode* plan)
    : ctx_(ctx), plan_(plan) {}

void InsertExecutor::Init() {
    executed_ = false;
    // Open the table before writing
    if (ctx_->table_handler && !plan_->table_name.empty()) {
        ctx_->table_handler->close();
        ctx_->table_handler->open(plan_->table_name.c_str());
    }
}

bool InsertExecutor::Next(Tuple* /*tuple*/, RID* /*rid*/) {
    if (executed_ || !ctx_->table_handler) return false;
    executed_ = true;

    if (!plan_->table_schema) return false;

    for (auto& row : plan_->value_rows) {
        // Evaluate expressions to produce values
        std::vector<Value> values(plan_->table_schema->GetColumnCount());

        // Map values to columns
        for (size_t i = 0; i < plan_->column_indices.size() && i < row.size(); i++) {
            int32_t col_idx = plan_->column_indices[i];
            if (col_idx >= 0 &&
                col_idx < static_cast<int32_t>(values.size())) {
                values[col_idx] = row[i]->Evaluate(nullptr, nullptr);
            }
        }

        auto tuple = Tuple::CreateFromValues(values, plan_->table_schema);
        ctx_->table_handler->write_row(tuple);
        ctx_->affected_rows++;
    }

    return false;  // INSERT produces no output rows
}

// =============================================================================
// UpdateExecutor
// =============================================================================

UpdateExecutor::UpdateExecutor(ExecutorContext* ctx,
                                 const UpdatePlanNode* plan,
                                 std::unique_ptr<AbstractExecutor> child)
    : ctx_(ctx), plan_(plan), child_(std::move(child)) {}

void UpdateExecutor::Init() {
    if (ctx_->table_handler && !plan_->table_name.empty()) {
        ctx_->table_handler->close();
        ctx_->table_handler->open(plan_->table_name.c_str());
    }
    if (child_) child_->Init();
    executed_ = false;
}

bool UpdateExecutor::Next(Tuple* /*tuple*/, RID* /*rid*/) {
    if (executed_ || !ctx_->table_handler || !child_) return false;
    executed_ = true;

    auto* schema = child_->GetOutputSchema();
    if (!schema) schema = plan_->table_schema;
    if (!schema) return false;

    // Phase 1: Collect all matching (RID, Tuple) pairs — we must NOT modify
    // the table while the child iterator holds page latches, or we deadlock.
    struct RowChange {
        RID rid;
        std::vector<Value> new_values;
    };
    std::vector<RowChange> changes;

    Tuple input_tuple;
    RID input_rid;
    while (child_->Next(&input_tuple, &input_rid)) {
        std::vector<Value> values;
        for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
            values.push_back(input_tuple.GetValue(schema, i));
        }
        for (auto& sc : plan_->set_clauses) {
            if (sc.col_idx >= 0 &&
                sc.col_idx < static_cast<int32_t>(values.size())) {
                values[sc.col_idx] = sc.value->Evaluate(&input_tuple, schema);
            }
        }
        changes.push_back({input_rid, std::move(values)});
    }
    // Child iterator is now exhausted — all page latches are released.

    // Phase 2: Apply all updates safely
    for (auto& ch : changes) {
        auto new_tuple = Tuple::CreateFromValues(ch.new_values, schema);
        ctx_->table_handler->update_row(ch.rid, new_tuple);
        ctx_->affected_rows++;
    }

    return false;
}

// =============================================================================
// DeleteExecutor
// =============================================================================

DeleteExecutor::DeleteExecutor(ExecutorContext* ctx,
                                 const DeletePlanNode* /*plan*/,
                                 std::unique_ptr<AbstractExecutor> child)
    : ctx_(ctx), child_(std::move(child)) {}

void DeleteExecutor::Init() {
    if (child_) child_->Init();
    executed_ = false;
}

bool DeleteExecutor::Next(Tuple* /*tuple*/, RID* /*rid*/) {
    if (executed_ || !ctx_->table_handler || !child_) return false;
    executed_ = true;

    // Phase 1: Collect all matching RIDs — avoid modifying while child iterator
    // holds page latches (would deadlock on WLatch vs RLatch).
    std::vector<RID> rids;
    Tuple input_tuple;
    RID input_rid;
    while (child_->Next(&input_tuple, &input_rid)) {
        rids.push_back(input_rid);
    }
    // Child iterator is now exhausted — all page latches are released.

    // Phase 2: Delete all matching rows safely
    for (auto& rid : rids) {
        ctx_->table_handler->delete_row(rid);
        ctx_->affected_rows++;
    }

    return false;
}

// =============================================================================
// CreateTableExecutor
// =============================================================================

CreateTableExecutor::CreateTableExecutor(ExecutorContext* ctx,
                                           const CreatePlanNode* plan)
    : ctx_(ctx), plan_(plan) {}

void CreateTableExecutor::Init() {
    executed_ = false;
}

bool CreateTableExecutor::Next(Tuple* /*tuple*/, RID* /*rid*/) {
    if (executed_ || !ctx_->table_handler) return false;
    executed_ = true;

    // Use handler->create() to create the table
    Schema schema_copy = plan_->schema;
    ctx_->table_handler->create(plan_->table_name.c_str(), &schema_copy);

    return false;
}

// =============================================================================
// DropTableExecutor
// =============================================================================

DropTableExecutor::DropTableExecutor(ExecutorContext* ctx,
                                       const DropPlanNode* plan)
    : ctx_(ctx), plan_(plan) {}

void DropTableExecutor::Init() {
    executed_ = false;
}

bool DropTableExecutor::Next(Tuple* /*tuple*/, RID* /*rid*/) {
    if (executed_ || !ctx_->table_handler) return false;
    executed_ = true;

    ctx_->table_handler->delete_table(plan_->table_name.c_str());

    return false;
}

// =============================================================================
// CreateIndexExecutor
// =============================================================================

CreateIndexExecutor::CreateIndexExecutor(ExecutorContext* ctx,
                                           const CreateIndexPlanNode* plan)
    : ctx_(ctx), plan_(plan) {}

void CreateIndexExecutor::Init() {
    executed_ = false;
}

bool CreateIndexExecutor::Next(Tuple* /*tuple*/, RID* /*rid*/) {
    if (executed_ || !ctx_->table_handler || !ctx_->catalog || !plan_->table_schema)
        return false;
    executed_ = true;

    // Build index by scanning the table
    page_id_t index_root = INVALID_PAGE_ID;

    if (plan_->index_type == "hash") {
        goods_db::ExtendibleHashIndex hash_idx(ctx_->bpm, index_root);
        // Scan and insert
        ctx_->table_handler->rnd_init(true);
        Tuple tuple;
        while (ctx_->table_handler->rnd_next(&tuple) == 0) {
            auto key_val = tuple.GetValue(plan_->table_schema,
                                           static_cast<uint32_t>(plan_->col_idx));
            hash_idx.Insert(key_val.GetAsBigInt(), tuple.GetRid());
        }
        ctx_->table_handler->rnd_end();
        index_root = hash_idx.GetDirectoryPageId();
    } else {
        // Default: B+Tree
        goods_db::BPlusTree btree(ctx_->bpm, index_root);
        ctx_->table_handler->rnd_init(true);
        Tuple tuple;
        while (ctx_->table_handler->rnd_next(&tuple) == 0) {
            auto key_val = tuple.GetValue(plan_->table_schema,
                                           static_cast<uint32_t>(plan_->col_idx));
            btree.Insert(key_val.GetAsBigInt(), tuple.GetRid());
        }
        ctx_->table_handler->rnd_end();
        index_root = btree.GetRootPageId();
    }

    // Register in catalog
    auto idx_type = (plan_->index_type == "hash") ? IndexInfo::IndexType::HASH
                                                   : IndexInfo::IndexType::BTREE;
    ctx_->catalog->CreateIndex(plan_->index_name, plan_->table_name,
                                plan_->table_schema->GetColumn(
                                    static_cast<uint32_t>(plan_->col_idx))
                                    .column_name,
                                idx_type, index_root);

    return false;
}

// =============================================================================
// ExecutorFactory
// =============================================================================

std::unique_ptr<AbstractExecutor> ExecutorFactory::Create(
    ExecutorContext* ctx, PlanNode* plan) {
    if (!plan) return nullptr;

    switch (plan->GetType()) {
        case PlanNodeType::SEQ_SCAN: {
            auto* p = static_cast<SeqScanPlanNode*>(plan);
            return std::make_unique<SeqScanExecutor>(ctx, p);
        }

        case PlanNodeType::FILTER: {
            auto* p = static_cast<FilterPlanNode*>(plan);
            auto child = Create(ctx, p->GetChildren().empty()
                                        ? nullptr
                                        : p->GetChildren()[0].get());
            return std::make_unique<FilterExecutor>(ctx, p, std::move(child));
        }

        case PlanNodeType::PROJECTION: {
            auto* p = static_cast<ProjectionPlanNode*>(plan);
            auto child = Create(ctx, p->GetChildren().empty()
                                        ? nullptr
                                        : p->GetChildren()[0].get());
            return std::make_unique<ProjectionExecutor>(ctx, p,
                                                         std::move(child));
        }

        case PlanNodeType::LIMIT: {
            auto* p = static_cast<LimitPlanNode*>(plan);
            auto child = Create(ctx, p->GetChildren().empty()
                                        ? nullptr
                                        : p->GetChildren()[0].get());
            return std::make_unique<LimitExecutor>(ctx, p, std::move(child));
        }

        case PlanNodeType::INSERT: {
            return std::make_unique<InsertExecutor>(ctx,
                                                      static_cast<InsertPlanNode*>(plan));
        }

        case PlanNodeType::UPDATE: {
            auto* p = static_cast<UpdatePlanNode*>(plan);
            auto child = Create(ctx, p->GetChildren().empty()
                                        ? nullptr
                                        : p->GetChildren()[0].get());
            return std::make_unique<UpdateExecutor>(ctx, p, std::move(child));
        }

        case PlanNodeType::DELETE: {
            auto* p = static_cast<DeletePlanNode*>(plan);
            auto child = Create(ctx, p->GetChildren().empty()
                                        ? nullptr
                                        : p->GetChildren()[0].get());
            return std::make_unique<DeleteExecutor>(ctx, p, std::move(child));
        }

        case PlanNodeType::CREATE: {
            return std::make_unique<CreateTableExecutor>(
                ctx, static_cast<CreatePlanNode*>(plan));
        }

        case PlanNodeType::DROP: {
            return std::make_unique<DropTableExecutor>(
                ctx, static_cast<DropPlanNode*>(plan));
        }

        case PlanNodeType::CREATE_INDEX: {
            return std::make_unique<CreateIndexExecutor>(
                ctx, static_cast<CreateIndexPlanNode*>(plan));
        }

        case PlanNodeType::SORT:
        case PlanNodeType::AGGREGATION:
        case PlanNodeType::TOP_N:
        case PlanNodeType::INDEX_SCAN:
        case PlanNodeType::HASH_JOIN:
        case PlanNodeType::NESTED_LOOP_JOIN:
            return CreateAdvancedExecutor(ctx, plan);

        default:
            return nullptr;
    }
}

}  // namespace goods_db
