#include "StreamSuppressor.h"

namespace cli_utils {

std::mutex SuppressStreams::mutex_;
int SuppressStreams::refCount_ = 0;
std::streambuf* SuppressStreams::originalCerr_ = nullptr;
std::streambuf* SuppressStreams::originalClog_ = nullptr;
std::unordered_set<std::streambuf*> SuppressStreams::devNullBufs_;
std::unordered_set<std::streambuf*> SuppressStreams::allDevNullBufs_;

}  // namespace cli_utils
