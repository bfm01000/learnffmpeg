#pragma once

#include <string>

struct ProbeOptions {
  std::string input;
  int packets = 80;
  std::string annexb_output;
};

void probe_media(const ProbeOptions& options);
