const unsigned char read_only_data[] = "read only";

int main(void)
{
    volatile unsigned char *pointer =
        (volatile unsigned char *)read_only_data;

    *pointer = 0;

    return 0;
}