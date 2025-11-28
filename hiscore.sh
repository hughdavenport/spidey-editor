last_good=1
down=1
for i in {1..62}; do
    if [[ "$down" -eq 1 ]]; then
        ./a.out patch $i tiles empty.txt room_north $((i+1)) room_south $((i+1)) gravity_vertical $((63)) gravity_horizontal 0
    else
        ./a.out patch $i tiles empty.txt room_north $((i+1)) room_south $((i+1)) gravity_vertical $((255-63)) gravity_horizontal 0
    fi
    switches=$(./a.out display $i |& grep 'Switches (length=' | cut -d '=' -f 2 | cut -d ')' -f 1)
    objects=$(./a.out display $i |& grep 'objects (length=' | cut -d '=' -f 2 | cut -d ')' -f 1)
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

# make mysterio's platform fire
# ./a.out patch 63 switches[1].chunks[1].on 0x5e

# make switch that we hit on entry kill mysterio
./a.out patch 63 switches[3].x 0x3 .y 0x14 .room_entry true .chunks[1].type TOGGLE_OBJECT .test 0x20
