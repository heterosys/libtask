#include "task.h"

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "clang/AST/AST.h"

#include "mmap.h"
#include "stream.h"

using std::binary_search;
using std::make_shared;
using std::pair;
using std::shared_ptr;
using std::sort;
using std::string;
using std::to_string;
using std::unordered_map;
using std::vector;

using clang::CharSourceRange;
using clang::CXXMemberCallExpr;
using clang::DeclGroupRef;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::ElaboratedType;
using clang::Expr;
using clang::ExprWithCleanups;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::LValueReferenceType;
using clang::MemberExpr;
using clang::RecordType;
using clang::SourceLocation;
using clang::Stmt;
using clang::TemplateSpecializationType;
using clang::VarDecl;
using clang::WhileStmt;

using llvm::dyn_cast;

const char kUtilFuncs[] = R"(namespace tlp{

template <typename T>
struct data_t {
  bool eos;
  T val;
};

template <typename T>
inline T read_fifo(data_t<T>& value, bool valid, bool* valid_ptr,
                    const T& def) {
#pragma HLS inline
#pragma HLS latency min = 1 max = 1
  if (valid_ptr) {
    *valid_ptr = valid;
  }
  return valid ? value.val : def;
}

template <typename T>
inline T read_fifo(hls::stream<data_t<T>>& fifo, data_t<T>& value,
                    bool& valid) {
#pragma HLS inline
#pragma HLS latency min = 1 max = 1
  T val = value.val;
  if (valid) {
    valid = fifo.read_nb(value);
  }
  return val;
}

template <typename T>
inline void write_fifo(hls::stream<data_t<T>>& fifo, const T& value) {
#pragma HLS inline
#pragma HLS latency min = 1 max = 1
  fifo.write({false, value});
}

template <typename T>
inline void close_fifo(hls::stream<data_t<T>>& fifo) {
#pragma HLS inline
#pragma HLS latency min = 1 max = 1
  fifo.write({true, {}});
}

}  // namespace tlp

)";

// Given a Stmt, find the first tlp::task in its children.
const ExprWithCleanups* GetTlpTask(const Stmt* stmt) {
  for (auto child : stmt->children()) {
    if (auto expr = dyn_cast<ExprWithCleanups>(child)) {
      if (expr->getType().getAsString() == "struct tlp::task") {
        return expr;
      }
    }
  }
  return nullptr;
}

// Given a Stmt, find all tlp::task::invoke's via DFS and update invokes.
void GetTlpInvokes(const Stmt* stmt,
                   vector<const CXXMemberCallExpr*>& invokes) {
  for (auto child : stmt->children()) {
    GetTlpInvokes(child, invokes);
  }
  if (const auto invoke = dyn_cast<CXXMemberCallExpr>(stmt)) {
    if (invoke->getRecordDecl()->getQualifiedNameAsString() == "tlp::task" &&
        invoke->getMethodDecl()->getNameAsString() == "invoke") {
      invokes.push_back(invoke);
    }
  }
}

// Given a Stmt, return all tlp::task::invoke's via DFS.
vector<const CXXMemberCallExpr*> GetTlpInvokes(const Stmt* stmt) {
  vector<const CXXMemberCallExpr*> invokes;
  GetTlpInvokes(stmt, invokes);
  return invokes;
}

// Return all loops that do not contain other loops but do contain FIFO
// operations.
void GetInnermostLoops(const Stmt* stmt, vector<const Stmt*>& loops) {
  for (auto child : stmt->children()) {
    if (child != nullptr) {
      GetInnermostLoops(child, loops);
    }
  }
  if (RecursiveInnermostLoopsVisitor().IsInnermostLoop(stmt)) {
    loops.push_back(stmt);
  }
}
vector<const Stmt*> GetInnermostLoops(const Stmt* stmt) {
  vector<const Stmt*> loops;
  GetInnermostLoops(stmt, loops);
  return loops;
}

// Apply tlp s2s transformations on a function.
bool TlpVisitor::VisitFunctionDecl(FunctionDecl* func) {
  if (func->hasBody() && func->isGlobal()) {
    const auto loc = func->getBeginLoc();
    if (context_.getSourceManager().isWrittenInMainFile(loc)) {
      // Insert utility functions before the first function.
      if (first_func_) {
        first_func_ = false;
        rewriter_.InsertTextBefore(loc, kUtilFuncs);
      }
      if (auto task = GetTlpTask(func->getBody())) {
        ProcessUpperLevelTask(task, func);
      } else {
        ProcessLowerLevelTask(func);
      }
    }
  }
  // Let the recursion continue.
  return true;
}

// Insert `#pragma HLS ...` after the token specified by loc.
bool TlpVisitor::InsertHlsPragma(const SourceLocation& loc,
                                 const string& pragma,
                                 const vector<pair<string, string>>& args) {
  string line{"\n#pragma HLS " + pragma};
  for (const auto& arg : args) {
    line += " " + arg.first;
    if (!arg.second.empty()) {
      line += " = " + arg.second;
    }
  }
  return rewriter_.InsertTextAfterToken(loc, line);
}

// Apply tlp s2s transformations on a upper-level task.
void TlpVisitor::ProcessUpperLevelTask(const ExprWithCleanups* task,
                                       const FunctionDecl* func) {
  const auto func_body = func->getBody();
  // TODO: implement qdma streams
  vector<StreamInfo> streams;
  for (const auto param : func->parameters()) {
    const string param_name = param->getNameAsString();
    if (IsMmap(param->getType())) {
      rewriter_.ReplaceText(
          param->getTypeSourceInfo()->getTypeLoc().getSourceRange(),
          GetMmapElemType(param) + "*");
      InsertHlsPragma(func_body->getBeginLoc(), "interface",
                      {{"m_axi", ""},
                       {"port", param_name},
                       {"offset", "slave"},
                       {"bundle", "gmem_" + param_name}});
    }
  }
  for (const auto param : func->parameters()) {
    InsertHlsPragma(func_body->getBeginLoc(), "interface",
                    {{"s_axilite", ""},
                     {"port", param->getNameAsString()},
                     {"bundle", "control"}});
  }
  InsertHlsPragma(
      func_body->getBeginLoc(), "interface",
      {{"s_axilite", ""}, {"port", "return"}, {"bundle", "control"}});
  rewriter_.InsertTextAfterToken(func_body->getBeginLoc(), "\n");

  // Process stream declarations.
  for (const auto child : func_body->children()) {
    if (const auto decl_stmt = dyn_cast<DeclStmt>(child)) {
      if (const auto var_decl = dyn_cast<VarDecl>(*decl_stmt->decl_begin())) {
        if (auto decl = GetTlpStreamDecl(var_decl->getType())) {
          const auto args = decl->getTemplateArgs().asArray();
          const string elem_type{args[0].getAsType().getAsString()};
          const string fifo_depth{args[1].getAsIntegral().toString(10)};
          const string var_name{var_decl->getNameAsString()};
          rewriter_.ReplaceText(
              var_decl->getSourceRange(),
              "hls::stream<tlp::data_t<" + elem_type + ">> " + var_name);
          InsertHlsPragma(child->getEndLoc(), "stream",
                          {{"variable", var_name}, {"depth", fifo_depth}});
        }
      }
    }
  }

  // Instanciate tasks.
  vector<const CXXMemberCallExpr*> invokes = GetTlpInvokes(task);
  string invokes_str{"#pragma HLS dataflow\n\n"};

  for (auto invoke : invokes) {
    int step = -1;
    if (const auto method = dyn_cast<MemberExpr>(invoke->getCallee())) {
      if (method->getNumTemplateArgs() != 1) {
        ReportError(method->getMemberLoc(),
                    "exactly 1 template argument expected")
            .AddSourceRange(CharSourceRange::getCharRange(
                method->getMemberLoc(),
                method->getEndLoc().getLocWithOffset(1)));
      }
      step = stoi(rewriter_.getRewrittenText(
          method->getTemplateArgs()[0].getSourceRange()));
    } else {
      ReportError(invoke->getBeginLoc(), "unexpected invocation: %0")
          .AddString(invoke->getStmtClassName());
    }
    invokes_str += "// step " + to_string(step) + "\n";
    for (unsigned i = 0; i < invoke->getNumArgs(); ++i) {
      const auto arg = invoke->getArg(i);
      if (const auto decl_ref = dyn_cast<DeclRefExpr>(arg)) {
        const string arg_name =
            rewriter_.getRewrittenText(decl_ref->getSourceRange());
        if (i == 0) {
          invokes_str += arg_name + "(";
        } else {
          if (i > 1) {
            invokes_str += ", ";
          }
          invokes_str += arg_name;
        }
      } else {
        ReportError(arg->getBeginLoc(), "unexpected argument: %0")
            .AddString(arg->getStmtClassName());
      }
    }
    invokes_str += ");\n";
  }
  // task->getSourceRange() does not include the final semicolon so we remove
  // the ending newline and semicolon from invokes_str.
  invokes_str.pop_back();
  invokes_str.pop_back();
  rewriter_.ReplaceText(task->getSourceRange(), invokes_str);

  // SDAccel only works with extern C kernels.
  rewriter_.InsertText(func->getBeginLoc(), "extern \"C\" {\n\n");
  rewriter_.InsertTextAfterToken(func->getEndLoc(), "\n\n}  // extern \"C\"\n");
}

// Apply tlp s2s transformations on a lower-level task.
void TlpVisitor::ProcessLowerLevelTask(const FunctionDecl* func) {
  // Find interface streams.
  vector<StreamInfo> streams;
  for (const auto param : func->parameters()) {
    if (auto ref = dyn_cast<LValueReferenceType>(
            param->getType().getCanonicalType().getTypePtr())) {
      if (auto decl = GetTlpStreamDecl(ref->getPointeeType())) {
        const auto args = decl->getTemplateArgs().asArray();
        string elem_type{
            args[0].getAsType().getUnqualifiedType().getAsString()};
        streams.emplace_back(param->getNameAsString(), elem_type);
        rewriter_.ReplaceText(
            param->getTypeSourceInfo()->getTypeLoc().getSourceRange(),
            "hls::stream<tlp::data_t<" + elem_type + ">>&");
      }
    } else if (IsMmap(param->getType())) {
      auto elem_type = GetMmapElemType(param);
      rewriter_.ReplaceText(
          param->getTypeSourceInfo()->getTypeLoc().getSourceRange(),
          elem_type + "*");
    }
  }

  // Retrieve stream information.
  const auto func_body = func->getBody();
  GetStreamInfo(func_body, streams, context_.getDiagnostics());

  // Before the original function body, insert data_pack pragmas.
  for (const auto& stream : streams) {
    InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                    {{"variable", stream.name}});
  }
  if (!streams.empty()) {
    rewriter_.InsertTextAfterToken(func_body->getBeginLoc(), "\n\n");
  }
  // Insert _x_value and _x_valid variables for read streams.
  string read_states;
  for (const auto& stream : streams) {
    if (stream.is_consumer && stream.need_peeking) {
      read_states += "tlp::data_t<" + stream.type + "> " + stream.ValueVar() +
                     "{false, {}};\n";
      read_states += "bool " + stream.ValidVar() + "{false};\n\n";
    }
  }
  rewriter_.InsertTextAfterToken(func_body->getBeginLoc(), read_states);

  // Rewrite stream operations via DFS.
  unordered_map<const CXXMemberCallExpr*, const StreamInfo*> stream_table;
  for (const auto& stream : streams) {
    for (auto call_expr : stream.call_exprs) {
      stream_table[call_expr] = &stream;
    }
  }
  RewriteStreams(func_body, stream_table);

  // Find loops that contain FIFOs operations but do not contain sub-loops;
  for (auto loop_stmt : GetInnermostLoops(func_body)) {
    auto stream_ops = GetTlpStreamOps(loop_stmt);
    sort(stream_ops.begin(), stream_ops.end());
    auto is_accessed = [&stream_ops](const StreamInfo& stream) -> bool {
      for (auto expr : stream.call_exprs) {
        if (binary_search(stream_ops.begin(), stream_ops.end(), expr)) {
          return true;
        }
      }
      return false;
    };

    // Is peeking buffer needed for this loop.
    bool need_peeking = false;
    for (const auto& stream : streams) {
      if (is_accessed(stream) && stream.is_consumer && stream.need_peeking) {
        need_peeking = true;
        break;
      }
    }

    auto mmap_ops = GetTlpMmapOps(loop_stmt);
    if (!mmap_ops.empty() && !need_peeking) {
      continue;
    }

    // Move increment statement to the end of loop body for ForStmt.
    if (const auto for_stmt = dyn_cast<ForStmt>(loop_stmt)) {
      const string inc =
          rewriter_.getRewrittenText(for_stmt->getInc()->getSourceRange());
      rewriter_.RemoveText(for_stmt->getInc()->getSourceRange());
      rewriter_.InsertText(for_stmt->getBody()->getEndLoc(), inc + ";\n");
    }

    // Find loop body.
    const Stmt* loop_body{nullptr};
    if (auto do_stmt = dyn_cast<DoStmt>(loop_stmt)) {
      loop_body = *do_stmt->getBody()->child_begin();
    } else if (auto for_stmt = dyn_cast<ForStmt>(loop_stmt)) {
      loop_body = *for_stmt->getBody()->child_begin();
    } else if (auto while_stmt = dyn_cast<WhileStmt>(loop_stmt)) {
      loop_body = *while_stmt->getBody()->child_begin();
    } else if (loop_stmt != nullptr) {
      ReportError(loop_stmt->getBeginLoc(), "unexpected loop: %0")
          .AddString(loop_stmt->getStmtClassName());
    } else {
      ReportError(func_body->getBeginLoc(), "null loop stmt");
    }

    string loop_preamble;
    for (const auto& stream : streams) {
      if (is_accessed(stream) && stream.is_consumer && stream.is_blocking) {
        if (!loop_preamble.empty()) {
          loop_preamble += " && ";
        }
        if (stream.need_peeking) {
          loop_preamble += stream.ValidVar();
        } else {
          loop_preamble += "!" + stream.name + ".empty()";
        }
      }
    }
    // Insert proceed only if there are blocking-read fifos.
    if (!loop_preamble.empty()) {
      loop_preamble = "bool " +
                      (StreamInfo::ProceedVar() + ("{" + loop_preamble)) +
                      "};\n\n";
      rewriter_.InsertText(loop_body->getBeginLoc(), loop_preamble,
                           /* InsertAfter= */ true,
                           /* indentNewLines= */ true);
      rewriter_.InsertText(loop_body->getBeginLoc(),
                           string{"if ("} + StreamInfo::ProceedVar() + ") {\n",
                           /* InsertAfter= */ true,
                           /* indentNewLines= */ true);
      rewriter_.InsertText(loop_stmt->getEndLoc(), "} else {\n",
                           /* InsertAfter= */ true,
                           /* indentNewLines= */ true);

      // If cannot proceed, still need to do state transition.
      string state_transition{};
      for (const auto& stream : streams) {
        if (is_accessed(stream) && stream.is_consumer && stream.need_peeking) {
          state_transition += "if (!" + stream.ValidVar() + ") {\n";
          state_transition += stream.ValidVar() + " = " + stream.name +
                              ".read_nb(" + stream.ValueVar() + ");\n";
          state_transition += "}\n";
        }
      }
      state_transition += "}  // if (" + StreamInfo::ProceedVar() + ")\n";
      rewriter_.InsertText(loop_stmt->getEndLoc(), state_transition,
                           /* InsertAfter= */ true,
                           /* indentNewLines= */ true);
    }
  }
}

void TlpVisitor::RewriteStreams(
    const Stmt* stmt,
    unordered_map<const CXXMemberCallExpr*, const StreamInfo*> stream_table,
    shared_ptr<unordered_map<const Stmt*, bool>> visited) {
  if (visited == nullptr) {
    visited = make_shared<decltype(visited)::element_type>();
  }
  if ((*visited)[stmt]) {
    return;
  }
  (*visited)[stmt] = true;

  for (auto child : stmt->children()) {
    if (child != nullptr) {
      RewriteStreams(child, stream_table);
    }
  }
  if (const auto call_expr = dyn_cast<CXXMemberCallExpr>(stmt)) {
    auto stream = stream_table.find(call_expr);
    if (stream != stream_table.end()) {
      RewriteStream(stream->first, *stream->second);
    }
  }
}

// Given the CXXMemberCallExpr and the corresponding StreamInfo, rewrite the
// code.
void TlpVisitor::RewriteStream(const CXXMemberCallExpr* call_expr,
                               const StreamInfo& stream) {
  string rewritten_text{};
  switch (GetStreamOp(call_expr)) {
    case StreamOpEnum::kTestEos: {
      rewritten_text =
          "(" + stream.ValidVar() + " && " + stream.ValueVar() + ".eos)";
      break;
    }
    case StreamOpEnum::kBlockingPeek:
    case StreamOpEnum::kNonBlockingPeek: {
      rewritten_text = stream.ValueVar() + ".val";
      break;
    }
    case StreamOpEnum::kBlockingRead: {
      if (stream.need_peeking) {
        rewritten_text = "tlp::read_fifo(" + stream.name + ", " +
                         stream.ValueVar() + ", " + stream.ValidVar() + ")";
      } else {
        rewritten_text = stream.name + ".read().val";
      }
      break;
    }
    case StreamOpEnum::kWrite: {
      rewritten_text =
          "tlp::write_fifo(" + stream.name + ", " + stream.type + "{" +
          rewriter_.getRewrittenText(call_expr->getArg(0)->getSourceRange()) +
          "})";
      break;
    }
    case StreamOpEnum::kClose: {
      rewritten_text = "tlp::close_fifo(" + stream.name + ")";
      break;
    }
    default: {
      auto callee = dyn_cast<MemberExpr>(call_expr->getCallee());
      auto diagnostics_builder =
          ReportError(callee->getMemberLoc(),
                      "tlp::stream::%0 has not yet been implemented");
      diagnostics_builder.AddSourceRange(CharSourceRange::getCharRange(
          callee->getMemberLoc(),
          callee->getMemberLoc().getLocWithOffset(
              callee->getMemberNameInfo().getAsString().size())));
      diagnostics_builder.AddString(
          call_expr->getMethodDecl()->getNameAsString());
      rewritten_text = "NOT_IMPLEMENTED";
    }
  }

  rewriter_.ReplaceText(call_expr->getSourceRange(), rewritten_text);
}
