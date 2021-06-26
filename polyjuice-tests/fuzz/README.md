# Polyjuice pre-compiled contracts fuzzing

## Setup the enviroment

0. Follow [these instructions](https://apt.llvm.org/) to install Clang on Linux. You can download Clang for Windows from [llvm’s snapshot builds page](https://llvm.org/builds/).

1. build-all-in-docker

2. [Build fuzz targets](https://github.com/google/fuzzing/blob/master/docs/building-fuzz-targets.md)


## Pre-compiled contracts

polyjuice-tests/src/test_cases/pre_compiled_contracts.rs

## test_contracts

```bash
cargo test --manifest-path ../Cargo.toml contracts -v
```

### General Algorithm

```pseudo code
// pseudo code
Instrument program for code coverage
for {
  Choose random input from corpus
  Mutate input
  Execute input and collect coverage
  If new coverage/paths are hit add it to corpus (corpus - directory with test-cases)
}
```

## Resources

- [libFuzzer Tutorial](https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md)
- [Download LLVM](https://releases.llvm.org/download.html)
- [What makes a good fuzz target](https://github.com/google/fuzzing/blob/master/docs/good-fuzz-target.md)
