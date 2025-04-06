#!/bin/bash

# Filepath to the compiled program
PROGRAM="./lab04_core_affine_linux"

# Output file for results
OUTPUT_FILE="results_core_affine_linux.txt"

# Matrix sizes and number of slaves to test
MATRIX_SIZES=(20000 25000 30000)
SLAVE_COUNTS=(2 4 8 16)

# Base port for communication
BASE_PORT=5000

# Clear the output file
echo "Experiment Results (Linux Core Affine)" > $OUTPUT_FILE
echo "======================================" >> $OUTPUT_FILE

# Run experiments
for MATRIX_SIZE in "${MATRIX_SIZES[@]}"; do
    for SLAVE_COUNT in "${SLAVE_COUNTS[@]}"; do
        echo "Matrix Size: $MATRIX_SIZE, Slaves: $SLAVE_COUNT" >> $OUTPUT_FILE
        echo "----------------------------------------" >> $OUTPUT_FILE

        for RUN in {1..3}; do
            echo "Run #$RUN" >> $OUTPUT_FILE

            # Start slave processes
            for ((i=1; i<=$SLAVE_COUNT; i++)); do
                SLAVE_PORT=$((BASE_PORT + i))
                $PROGRAM $MATRIX_SIZE $SLAVE_PORT 1 > slave_$i.log 2>&1 &
            done

            # Allow slaves to initialize
            sleep 2

            # Run the master process and capture its output
            $PROGRAM $MATRIX_SIZE $BASE_PORT 0 $SLAVE_COUNT >> $OUTPUT_FILE 2>&1

            # Wait for all slave processes to finish
            wait

            echo "----------------------------------------" >> $OUTPUT_FILE
        done
    done
done

echo "Experiments completed. Results saved in $OUTPUT_FILE."