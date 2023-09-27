#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <numeric>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <queue>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

// Data structure for storing n-grams and their counts
using ngrams_map = std::unordered_map<std::string, std::atomic_int>;

// --------------------------------------------------------

// Global variables (shared by threads)
int num_threads = 4;                // number of threads (default: 4)
std::string whitelist = "";         // whitelist of allowed characters (default: none)
int n = 3;                          // size of n-grams (default: 3)
std::atomic_int num_files_done(0);  // number of files processed
std::mutex files_mutex;             // mutex for accessing files queue
std::condition_variable files_cv;   // condition variable for files queue
std::queue<fs::path> files_queue;   // queue of files to be processed
std::mutex ngrams_mutex;            // mutex for accessing ngrams map
ngrams_map ngrams;                  // map of n-grams and their counts

// --------------------------------------------------------

// Function to check if a character is allowed according to the whitelist
bool is_allowed(char c) {
    if (whitelist.empty()) {
        return true;
    } else {
        return whitelist.find(c) != std::string::npos;
    }
}

// Function to compute (whitelisted) n-grams of the string
void process_string_and_update_ngrams(const std::string& str) {
    std::string filtered_text;
    if (whitelist != "") {
        // If whitelist is specified, only keep letters in the whitelist
        for (char c : str) {
            if (is_allowed(c)) {
                filtered_text.push_back(c);
            }
        }
    } else {
        // If no whitelist specified, keep all letters
        filtered_text = str;
    }

    // Update counts for n-grams
    for (std::size_t i = 0; i <= filtered_text.size() - n; i++) {
        if (!filtered_text.empty()) {
            std::string ngram = filtered_text.substr(i, n);
            {
                // Use a lock_guard to ensure thread safety when updating the global counts
                std::lock_guard<std::mutex> lock(ngrams_mutex);
                ngrams[ngram]++;
            }
        }
    }
}

// Recursive function to list all files in a folder (excluding excluded folders) with a given extension
void list_files(const fs::path& root_path, const std::vector<fs::path>& excluded_folders, const std::string& extension) {
    std::vector<fs::path> excluded_canonical;
    for (const auto& excluded : excluded_folders) {
        try {
            excluded_canonical.push_back(fs::canonical(excluded));
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error getting canonical path for excluded folder " << excluded << ": " << e.what() << std::endl;
        }
    }

    std::stack<fs::path> folders;
    folders.push(root_path);

    while (!folders.empty()) {
        const auto current_path = folders.top();
        folders.pop();

        bool is_excluded = false;
        for (const auto& excluded : excluded_canonical) {
            try {
                if (fs::equivalent(fs::canonical(current_path), excluded)) {
                    is_excluded = true;
                    break;
                }
            } catch (const std::exception& ex) {
                std::cerr << "Error getting canonical path for " << current_path << ": " << ex.what() << std::endl;
            }
        }
        if (is_excluded) {
            continue;
        }

        try {
            for (const auto& entry : fs::directory_iterator(current_path)) {
                if (fs::is_directory(entry)) {
                    folders.push(entry.path());
                } else if (fs::is_regular_file(entry) && entry.path().extension() == extension) {
                    std::lock_guard<std::mutex> lock(files_mutex);
                    files_queue.push(entry.path());
                    files_cv.notify_one();
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "Error accessing file or folder: " << ex.what() << std::endl;
        }
    }
}

// Function that calls n-grams on one file, if the file can be opened
void process_file(const fs::path& file_path) {
    try {
        if (fs::is_regular_file(file_path)) {
            std::ifstream file(file_path.string());
            if (file.is_open()) {
                // check if file is empty
                char firstChar = file.peek();
                if (firstChar == std::ifstream::traits_type::eof()) {
                    // empty file
                    std::cerr << std::endl << "Error empty file " << file_path << std::endl;
                } else {
                    // file not empty: rewind to the beginning of the file
                    file.seekg(0);
                    // process the file
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    process_string_and_update_ngrams(buffer.str());
                }

                file.close();
            } else {
                std::cerr << std::endl << "Error opening file " << file_path << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << std::endl << "Error processing file " << file_path << ": " << e.what() << std::endl;
    }

    // Increment the number of processed files
    num_files_done++;
}

// Function that calls n-grams on all the files
void process_files() {
    // Loop until all files have been processed
    while (true) {
        // Get the next file path from the queue
        std::unique_lock<std::mutex> lock(files_mutex);
        files_cv.wait(lock, [](){return !files_queue.empty();}); // wait until queue is not empty
        auto file_path = files_queue.front();
        files_queue.pop();
        // std::cout << "DEBUG. Processing file: " << file_path << std::endl;
        lock.unlock(); // unlock here to allow other threads to access the queue

        process_file(file_path);

        if (files_queue.empty()) {
            break; // all files have been processed
        }
    }
}

// Function to display a progress bar
void display_progress(int num_files_total) {
    int bar_width = 100;
    std::chrono::system_clock::time_point start_time = std::chrono::system_clock::now();
    while (true) {
        int num_files_done_local = num_files_done.load();
        if (num_files_done_local == num_files_total) {
            break;
        }
        float progress = static_cast<float>(num_files_done_local) / num_files_total;
        int bar_filled = static_cast<int>(progress * bar_width);
        std::chrono::duration<double> elapsed_time = std::chrono::system_clock::now() - start_time;
	// '█' '░'
        std::cout << "\r[" << std::string(bar_filled, '=') << std::string(bar_width - bar_filled, '.')
            << "] " << static_cast<int>(progress * 100) << "% - " << num_files_done_local << "/"
            << num_files_total << " files - elapsed time: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed_time).count() << "s"
            << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << std::endl;
}


// Define a custom comparator function to sort the map by value (count)
bool compare_by_value(const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
    return a.second > b.second;
}

// Function to write the n-grams to CSV file
void write_ngrams_csv(const std::string& csv_file_path, bool sort_values=true) {
    std::ofstream csv_file(csv_file_path);

    // auto csv_delimiter = "⌱";
    const char* csv_delimiter = "\t";

    // header
    csv_file << "n-gram" << csv_delimiter << "count\n";

    if (!sort_values){
        // write values to csv
        for (const auto& [ngram, count] : ngrams) {
            csv_file << ngram << "," << count << "\n";
        }
    } else {
        std::cerr << "Sorting n-grams by count (decreasing)..." << std::endl;
        // Convert the map to a vector of pairs
        std::vector<std::pair<std::string, int>> ngrams_vector;
        ngrams_vector.reserve(ngrams.size());
        for (const auto& ngram : ngrams) {
            ngrams_vector.emplace_back(ngram.first, ngram.second.load());
        }
        // Sort the vector by count in decreasing order
        std::sort(ngrams_vector.begin(), ngrams_vector.end(), compare_by_value);
        // write values to csv
        for (const auto& [ngram, count] : ngrams_vector) {
            csv_file << ngram << csv_delimiter << count << "\n";
        }
    }
    csv_file.close();
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <folder> [<options>]" << std::endl;
        return 1;
    }
    std::string folder_path_str(argv[1]);
    fs::path folder_path(folder_path_str);
    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
        std::cerr << "Error: " << folder_path_str << " is not a valid directory." << std::endl;
        return 1;
    }
    std::vector<fs::path> excluded_folders;
    for (int i = 2; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--whitelist") {
            i++;
            if (i == argc) {
                std::cerr << "Error: expected whitelist after --whitelist." << std::endl;
                return 1;
            }
            whitelist = argv[i];
        } else if (arg == "--n") {
            i++;
            if (i == argc) {
                std::cerr << "Error: expected integer after --n." << std::endl;
                return 1;
            }
            n = std::stoi(argv[i]);
            if (n <= 0) {
                std::cerr << "Error: n must be a positive integer." << std::endl;
                return 1;
            }
        } else if (arg == "--exclude") {
            i++;
            if (i == argc) {
                std::cerr << "Error: expected directory path after --exclude." << std::endl;
                return 1;
            }
            excluded_folders.push_back(fs::path(argv[i]));
        } else if (arg == "--threads") {
            i++;
            if (i == argc) {
                std::cerr << "Error: expected integer after --threads." << std::endl;
                return 1;
            }
            num_threads = std::stoi(argv[i]);
            if (num_threads <= 0) {
                std::cerr << "Error: number of threads must be a positive integer." << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: unrecognized option " << arg << std::endl;
            return 1;
        }
    }

    // List files to be processed
    std::string extension = ".txt";
    list_files(folder_path, excluded_folders, extension);
    int num_files_total = files_queue.size();
    if (num_files_total == 0) {
        std::cerr << "Error: no files to process." << std::endl;
        return 1;
    }
    std::cout << "Processing " << num_files_total << " files..." << std::endl;

    // Display progress bar
    std::thread progress_thread(display_progress, num_files_total);

    // Start threads to process files
    std::vector<std::thread> threads(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads[i] = std::thread(process_files);
    }

    // Wait for threads to finish
    for (int i = 0; i < num_threads; i++) {
        threads[i].join();
    }

    // Stop progress bar
    progress_thread.join();

    // Write n-grams to CSV and JSON files
    write_ngrams_csv("ngrams.csv");
    // write_ngrams_json("ngrams.json");

    std::cout << "Done." << std::endl;
    return 0;
}
