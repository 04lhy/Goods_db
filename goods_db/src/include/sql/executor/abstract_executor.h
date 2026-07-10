#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/rid.h"
#include "sql/planner/plan_nodes.h"
#include "storage/table/tuple.h"
#include "type/schema.h"

namespace goods_db {

// Forward declarations
class handler;
class Catalog;
class BufferPoolManager;
class DiskManager;

// =============================================================================
// AbstractExecutor — Volcano-style iterator model
// =============================================================================

class AbstractExecutor {
public:
    AbstractExecutor() = default;
    virtual ~AbstractExecutor() = default;

    /** Initialize the executor (called before first Next()) */
    virtual void Init() = 0;

    /** Get the next tuple. Returns false when exhausted. */
    virtual bool Next(Tuple* tuple, RID* rid) = 0;

    /** Get the output schema of this executor */
    virtual const Schema* GetOutputSchema() = 0;
};

// =============================================================================
// ExecutorContext — shared context for all executors in a tree
// =============================================================================

struct ExecutorContext {
    handler* table_handler{nullptr};
    Catalog* catalog{nullptr};
    BufferPoolManager* bpm{nullptr};
    DiskManager* disk_manager{nullptr};
};

// =============================================================================
// SeqScanExecutor — full table scan via TableIterator
// =============================================================================

class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(ExecutorContext* ctx, const SeqScanPlanNode* plan);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return plan_->table_schema; }

private:
    ExecutorContext* ctx_;
    const SeqScanPlanNode* plan_;
    bool scan_started_{false};
};

// =============================================================================
// FilterExecutor — evaluate predicate, pass matching tuples
// =============================================================================

class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(ExecutorContext* ctx, const FilterPlanNode* plan,
                   std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override {
        return child_->GetOutputSchema();
    }

private:
    const FilterPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
};

// =============================================================================
// ProjectionExecutor — compute output expressions from input tuples
// =============================================================================

class ProjectionExecutor : public AbstractExecutor {
public:
    ProjectionExecutor(ExecutorContext* ctx, const ProjectionPlanNode* plan,
                       std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return &output_schema_; }

private:
    const ProjectionPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
    Schema output_schema_;
};

// =============================================================================
// LimitExecutor — skip offset rows, return up to limit rows
// =============================================================================

class LimitExecutor : public AbstractExecutor {
public:
    LimitExecutor(ExecutorContext* ctx, const LimitPlanNode* plan,
                  std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override {
        return child_->GetOutputSchema();
    }

private:
    const LimitPlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
    int64_t returned_{0};
    int64_t skipped_{0};
};

// =============================================================================
// InsertExecutor — insert rows via handler->write_row()
// =============================================================================

class InsertExecutor : public AbstractExecutor {
public:
    InsertExecutor(ExecutorContext* ctx, const InsertPlanNode* plan);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return nullptr; }

private:
    ExecutorContext* ctx_;
    const InsertPlanNode* plan_;
    bool executed_{false};
};

// =============================================================================
// UpdateExecutor — update rows found by child executor
// =============================================================================

class UpdateExecutor : public AbstractExecutor {
public:
    UpdateExecutor(ExecutorContext* ctx, const UpdatePlanNode* plan,
                   std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return nullptr; }

private:
    ExecutorContext* ctx_;
    const UpdatePlanNode* plan_;
    std::unique_ptr<AbstractExecutor> child_;
    bool executed_{false};
};

// =============================================================================
// DeleteExecutor — delete rows found by child executor
// =============================================================================

class DeleteExecutor : public AbstractExecutor {
public:
    DeleteExecutor(ExecutorContext* ctx, const DeletePlanNode* plan,
                   std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return nullptr; }

private:
    ExecutorContext* ctx_;
    std::unique_ptr<AbstractExecutor> child_;
    bool executed_{false};
};

// =============================================================================
// DDL Executors
// =============================================================================

class CreateTableExecutor : public AbstractExecutor {
public:
    CreateTableExecutor(ExecutorContext* ctx, const CreatePlanNode* plan);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return nullptr; }

private:
    ExecutorContext* ctx_;
    const CreatePlanNode* plan_;
    bool executed_{false};
};

class DropTableExecutor : public AbstractExecutor {
public:
    DropTableExecutor(ExecutorContext* ctx, const DropPlanNode* plan);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return nullptr; }

private:
    ExecutorContext* ctx_;
    const DropPlanNode* plan_;
    bool executed_{false};
};

class CreateIndexExecutor : public AbstractExecutor {
public:
    CreateIndexExecutor(ExecutorContext* ctx, const CreateIndexPlanNode* plan);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid) override;
    const Schema* GetOutputSchema() override { return nullptr; }

private:
    ExecutorContext* ctx_;
    const CreateIndexPlanNode* plan_;
    bool executed_{false};
};

// =============================================================================
// ExecutorFactory — creates executor tree from PlanNode tree
// =============================================================================

class ExecutorFactory {
public:
    /**
     * Create an executor tree from a plan node tree.
     * @param ctx Shared executor context
     * @param plan Root of the plan node tree
     * @return Root executor of the created tree
     */
    static std::unique_ptr<AbstractExecutor> Create(ExecutorContext* ctx,
                                                     PlanNode* plan);
};

}  // namespace goods_db
