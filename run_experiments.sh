#!/bin/bash

PROGRAM="./lab04"
OUTPUT_FILE="results.txt"
MATRIX_SIZES=(20000 25000 30000)
SLAVE_COUNTS=(2 4 8 16)
BASE_PORT=5000

echo "Experiment Results" > $OUTPUT_FILE
echo "=================" >> $OUTPUT_FILE

for MATRIX_SIZE in "${MATRIX_SIZES[@]}"; do
    for SLAVE_COUNT in "${SLAVE_COUNTS[@]}"; do
        echo "Matrix: $MATRIX_SIZE, Slaves: $SLAVE_COUNT" >> $OUTPUT_FILE
        
        for RUN in {1..3}; do
            echo "Run $RUN" >> $OUTPUT_FILE
            
            # Start slaves
            for ((i=1; i<=$SLAVE_COUNT; i++)); do
                SLAVE_PORT=$((BASE_PORT + i))
                $PROGRAM $MATRIX_SIZE $SLAVE_PORT 1 >> slave_$i.log 2>&1 &
            done
            sleep 2
            
            # Run master with slave count parameter
            $PROGRAM $MATRIX_SIZE $BASE_PORT 0 $SLAVE_COUNT >> $OUTPUT_FILE 2>&1
            
            wait
            echo "-----" >> $OUTPUT_FILE
        done
    done
done

echo "Experiments completed. Results in $OUTPUT_FILE"