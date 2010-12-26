
#include "encoder.cpp"

int main(int, char* x[]) {
	unsigned char c[8];

	Encoder::fromBase32(x[1], c, 8);
	
	for(int i = 0; i < 8; i++)
		printf("%02x", (unsigned int)c[i]);
	printf("\n");
	for(int i = 0; x[1][i]; i++)
		printf("%02x", (unsigned int)x[1][i]);

	printf("\n");

}

