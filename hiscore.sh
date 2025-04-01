last_good=1
down=1
for i in {1..62}; do
    if [[ "$down" -eq 1 ]]; then
        ./a.out patch $i tiles empty.txt room_north $((i+1)) room_south $((i+1)) gravity_vertical $((63)) gravity_horizontal 0
    else
        ./a.out patch $i tiles empty.txt room_north $((i+1)) room_south $((i+1)) gravity_vertical $((255-63)) gravity_horizontal 0
    fi
    switches=$(./a.out display $i |& grep 'number.*switches' | cut -d ':' -f 2 | tr -d ' ')
    objects=$(./a.out display $i |& grep 'number.*objects' | cut -d ':' -f 2 | tr -d ' ')
    for j in $(seq 0 $switches); do
        ./a.out delete $i switches[0]
    done
    for j in $(seq 0 $objects); do
        ./a.out delete $i objects[0]
    done
    last_good=$i
    if [[ "$down" -eq 1 ]]; then
        down=0;
    else
        down=1
    fi
done
./a.out patch 63 switches[1].chunks[1].on 0x5e
