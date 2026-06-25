#!/bin/bash

#variables with error codes
ERR_FILE_NOT_FOUND=1
ERR_INVALID_PARSING=2
ERR_ENV_MISSING=3
ERR_SYS_FAILURE=5
ERR_NOT_READABLE=6
ERR_NOT_VALID_VALUE=7

#function that control if a variable is a positive number
function control_positive_integer(){
    local value="$1"
    local name_parameter="$2"

    if [[ ! "$value" =~ ^[0-9]+$ ]] || (("$value" <= 0)); then
        echo "error: $ERR_NOT_VALID_VALUE, $name_parameter value isn't valid"
        exit $ERR_NOT_VALID_VALUE
    fi
}

#function that control if a variable is a string
function control_string(){
    local value="$1"
    local name_parameter="$2"

    if [[ "$value" =~ ^[0-9]+$ ]]; then
        echo "error: $ERR_NOT_VALID_VALUE, $name_parameter value isn't valid"
        exit $ERR_NOT_VALID_VALUE
    fi
}

#inizialize default .env file
env_file=".env"
parameters=()   #contains parameters of .env to change
values=()       #values associated with the parameters

#for each argument control if is a env file or a parameter
for arg in "$@"; do
    if [[ "$arg" == --env-file=* ]]; then
        env_file="${arg#*=}"

    elif [[ "$arg" =~ ^--[^=]+=.+$ ]]; then
        parameters+=("${arg%%=*}")
        values+=("${arg#*=}")

    else
        echo "error: $ERR_INVALID_PARSING, format not valid"
        exit $ERR_INVALID_PARSING
    fi
done

#check that .env exists
[[ -e "$env_file" ]] || { echo "error: $ERR_FILE_NOT_FOUND, file ($env_file) doesn'exist"; exit $ERR_FILE_NOT_FOUND; }

#check that .env is readable
[[ ! -r "$env_file" ]] && { echo "error: $ERR_NOT_READABLE, file ($env_file) isn't readable"; exit $ERR_NOT_READABLE; }

#check that the .env file contains all the lines
[[ $(grep -v '^\s*$' "$env_file" | wc -l) -ne 8 ]] && { 
    echo "error: $ERR_INVALID_PARSING, the env file shoud have 8 paramaters"
    exit $ERR_INVALID_PARSING; 
}

#overwrites .env parameters given in input
for i in "${!parameters[@]}"; do
    tmp_param="${parameters[$i]#--}"
	parameter=$(echo "$tmp_param" | tr '-' '_' | tr '[:lower:]' '[:upper:]')
	sed -i "s/$parameter=.*/$parameter=${values[$i]}/" "$env_file"
done

declare -A viewed   #used to control that there isn't duplicated parameters

#check that the .env parameters have valid values
while IFS= read -r line; do

    #ignore empty lines
    [[ -z "$line" ]] && continue

    #separate parameter name from value
    key=$(cut -d "=" -f 1 <<< "$line")
    value=$(cut -d "=" -f 2 <<< "$line")

    #check if the file contain multiple lines with the same parameter name
    if [[ -n "${viewed[$key]}" ]]; then
        echo "error: $ERR_INVALID_PARSING, $key is already setted"
        exit $ERR_INVALID_PARSING
    fi

    viewed[$key]=1


    #do checks to parameters
    case "$key" in
        "NUM_COOKS")
            num_cooks="$value"
            control_positive_integer "$num_cooks" "NUM_COOKS"
        ;;

        "NUM_WAITERS")
            num_waiters="$value"
            control_positive_integer "$num_waiters" "NUM_WAITERS"
        ;;

        "MAX_CUSTOMERS")
            max_costumers="$value"
            control_positive_integer "$max_costumers" "MAX_CUSTOMERS"
        ;;

        "TOTAL_CUSTOMERS")
            total_customers="$value"
            control_positive_integer "$total_customers" "TOTAL_CUSTOMERS"
        ;;

        "MENU_FILE")
            menu_file="$value"
        ;;

        "RESOURCES_FILE")
            resources_file="$value"
        ;;

        "GAME_SPEED")
            game_speed="$value"
            control_positive_integer "$game_speed" "GAME_SPEED"
        ;;
        
        "RANDOM_SEED")
            random_seed="$value"
            control_positive_integer "$random_seed" "RANDOM_SEED"
        ;;

        *)
            echo "error: $ERR_INVALID_PARSING, $line has not a valid format"
            exit $ERR_INVALID_PARSING
        ;;
    esac

    export "$key"="$value"      #set environment variables caught in the main of restaurant.c
done < "$env_file"

#check that "menu" and "resources" file exist
[[ -e "$menu_file" ]] || { echo "error: $ERR_FILE_NOT_FOUND, menu file doesn't exist"; exit $ERR_FILE_NOT_FOUND; }
[[ -e "$resources_file" ]] || { echo "error: $ERR_FILE_NOT_FOUND, resources file doesn't exist"; exit $ERR_FILE_NOT_FOUND; }

#check on CSV menu structure
while IFS= read -r line; do
    
    #ignore empty lines and first line
    [[ -z "$line" ]] && continue
    [[ "$line" == "name,price,time,requirements" ]] && continue

    #acquire number of columns
    num_columns=$(awk -F, '{print NF}' <<< "$line")

    #control if columns are the right number
    [[ "$num_columns" -ne 4 ]] && {
        echo "error $ERR_INVALID_PARSING, CSV menu shoud have 4 columns"
        exit $ERR_INVALID_PARSING
    }

    #control if the name is a string
    name=$(cut -d "," -f 1 <<< "$line")
    control_string "$name" "menu: name ("$line")"

    #control if the price is a positive integer
    price=$(cut -d "," -f 2 <<< "$line")
    control_positive_integer "$price" "menu: price ("$line")"

    #control if the time is a positive integer
    time=$(cut -d "," -f 3 <<< "$line")
    control_positive_integer "$time" "menu: time ("$line")"

    #control if requirements have a correct format
    requirements=$(cut -d "," -f 4 <<< "$line")
    requirements=$(tr ';' '\n'<<< "$requirements")

    while IFS= read -r single_resource; do
        if grep -q ":" <<< "$single_resource"; then
            resource=$(cut -d ":" -f 1 <<< "$single_resource")
            quantity=$(cut -d ":" -f 2 <<< "$single_resource")
            control_positive_integer "$quantity" "menu: requirments: quantity ("$line")"
        else
            resource="$single_resource"
        fi
        control_string "$resource" "menu: requirments: resource ($line)"
    done <<< "$requirements"

done < "$menu_file"

#check on CSV resources structure
while IFS= read -r line; do

    #ignore empty lines and first line
    [[ -z "$line" ]] && continue
    [[ "$line" == "resource,quantity,clean_time" ]] && continue

    #acquire number of columns
    num_columns=$(awk -F, '{print NF}' <<< "$line")

    #control if columns are the right number
    [[ "$num_columns" -ne 3 ]] && {
        echo "error $ERR_INVALID_PARSING, CSV resources shoud have 3 columns"
        exit $ERR_INVALID_PARSING
    }

    #control if the name of the resource is a string
    name=$(cut -d "," -f 1 <<< "$line")
    control_string "$name" "resource: name ("$line")"

    #control if the quantity is a positive integer
    quantity=$(cut -d "," -f 2 <<< "$line")
    control_positive_integer "$quantity" "resource: quantity ("$line")"

    # control if the clean time is a posititve integer
    clean_time=$(cut -d "," -f 3 <<< "$line")
    control_positive_integer "$clean_time" "resource: clean_time ("$line")"

done < "$resources_file"

if [[ ! -f "./restaurant" ]]; then
    echo "Error: the executable does not exist. Compile first with 'make build'."
    exit $ERR_SYS_FAILURE
fi

echo "Main is running"

exec ./restaurant
