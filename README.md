# Insta-ngram

This repository holds a fast (safely multi-threaded) code for computing n-grams. It can be useful for very large text datasets, where parallel processing is needed in order to compute n-grams efficiently.

N-grams have several applications in natural language processing (NLP), even at the age of Large Language Models (LLMs). Using n-grams can be much faster and cheaper than LLMs for very simple tasks such as simple text classification or language identification.

This program is stable and has been successfully tested on millions of documents. It also includes a progress bar that allows the user to see the progress over time.

## Getting started

First compile:

```bash
g++ -o ngrams ngrams.cpp -std=c++17 -pthread -lstdc++fs
```

and run:

```bash
./ngrams /home/you/path/to/your/text/data/ --n 3 --threads 8
```

## Options

You can change the value for `n` as well as other self-explanatory arguments:

* `--n 5`
* `--whitelist allowed_alphabet`
* `--exclude folders_to_skip`
* `--threads 8`

