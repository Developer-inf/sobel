#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>

#define MAX_FNAME	64
#define FILEHEADER	14
#define INFOHEADER	40

char fName[MAX_FNAME] = "screen.bmp";
char *outName = "copy.bmp";
int sobelMatrix[3][3] = {
	{ 1,	2,	1 },
	{ 0,	0,	0 },
	{ -1,	-2,	-1 }
};

typedef struct {
	int *sobelArr;
	int *pixArr;
	int y;
	int width;
} Args;

//turning image to blackwhite colors
int turnToGrey(int x) {
	int res = (((x >> 16) & 255)+((x >> 8) & 255)+(x & 255)) / 3;
	return (res << 16) | (res << 8) | res;
}

//gradient for Y
int sumY(int* arr, int x, int y, int width) {
	int res = 0;
	res += (arr[(y - 1) * width + x - 1] & 255) * sobelMatrix[0][0];
	res += (arr[(y - 1) * width + x    ] & 255) * sobelMatrix[0][1];
	res += (arr[(y - 1) * width + x + 1] & 255) * sobelMatrix[0][2];
	res += (arr[(y + 1) * width + x - 1] & 255) * sobelMatrix[2][0];
	res += (arr[(y + 1) * width + x    ] & 255) * sobelMatrix[2][1];
	res += (arr[(y + 1) * width + x + 1] & 255) * sobelMatrix[2][2];
	return res;
}

//gradient for X
int sumX(int* arr, int x, int y, int width) {
	int res = 0;
	res += (arr[(y - 1) * width + x - 1] & 255) * sobelMatrix[2][0];
	res += (arr[(y    ) * width + x - 1] & 255) * sobelMatrix[2][1];
	res += (arr[(y + 1) * width + x - 1] & 255) * sobelMatrix[2][2];
	res += (arr[(y - 1) * width + x + 1] & 255) * sobelMatrix[0][0];
	res += (arr[(y    ) * width + x + 1] & 255) * sobelMatrix[0][1];
	res += (arr[(y + 1) * width + x + 1] & 255) * sobelMatrix[0][2];
	return res;
}

//work for threads
void *work(void *input) {
	Args data = *(Args*)input;
	int *sobelArr = data.sobelArr;
	int *pixArr = data.pixArr;
	int y = data.y;
	int width = data.width;

	for(int x = 1; x < width - 1; x++) {
		int tmp = 0;
		int tmpX = sumX(pixArr, x, y, width);
		int tmpY = sumY(pixArr, x, y, width);
		tmp = (int)sqrt((tmpX * tmpX + tmpY * tmpY));
		tmp = (tmp > 0) ? (tmp > 255) ? 255 : tmp : 0;
		sobelArr[y * width + x] = (tmp << 16) | (tmp << 8) | tmp;
	}
	return NULL;
}

int main() {
	int rfd, wfd;	//file descriptord for read and write respectievly
	char fileHeader[FILEHEADER];	//14 bytes for header
	char infoHeader[INFOHEADER];	//40 bytes for info
	int fileSize = 0;			//image file size
	int width = 0, height = 0;	//width and height of image
	int padding = 0;			//padding at the end of each row
	int tmp = 0;		//temporary var
	int *pixArr;		//array of pixels of original image
	int *sobelArr;		//array of processed original pixels
	char buf[16000];	//buffer for reading whole row of image
	pthread_t *th;
	Args *data;

	printf("Enter filename: ");
//	scanf("%s", fName);

	//open image
	if((rfd = open(fName, O_RDONLY)) < 0) return 1;

	//reading header and info [14 + 40] bytes
	if(read(rfd, fileHeader, FILEHEADER) <= 0) return 2;
	if(read(rfd, infoHeader, INFOHEADER) <= 0) return 3;

	//getting nrcessary data from metadata
	for(int i = 0; i < 4; i++) {
		fileSize |= ((fileHeader[2 + i] << 8 * i) & (0xff << 8 * i));
		width |= ((infoHeader[4 + i] << 8 * i) & (0xff << 8 * i));
		height |= ((infoHeader[8 + i] << 8 * i) & (0xff << 8 * i));
	}
	printf("File size: %d\nWidth: %d\nHeight: %d\n", fileSize, width, height);

	//allocating memory for pixels
	pixArr = (int*)malloc(width * height * sizeof(int));
	sobelArr = (int*)malloc(width * height * sizeof(int));
	th = (pthread_t*)malloc((height - 2) * sizeof(pthread_t));
	data = (Args*)malloc((height - 2) * sizeof(Args));
	padding = ((4 - (width * 3) % 4) % 4);

	for(int y = 0; y < height; y++) {
		read(rfd, buf, width * 3);	//read row
		for(int x = 0; x < width; x++) {
			pixArr[y * width + x] = (	//save row
					((buf[3*x] << 16) & 0xff0000) | 
					((buf[3*x+1] << 8) & 0xff00) | 
					((buf[3*x+2] ) & 0xff)
					);
			pixArr[y * width + x] = turnToGrey(pixArr[y * width + x]);
		}
		read(rfd, &tmp, padding);	//read padding
	}
	close(rfd);
	
	//opening file to write processed image
	if((wfd = open(outName, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) return 4;

	//write metadata
	write(wfd, fileHeader, FILEHEADER);
	write(wfd, infoHeader, INFOHEADER);

	//distribute work for "height-1" threads
	for(int y = 1; y < height - 1; y++) {
		data[y-1].sobelArr = sobelArr;
		data[y-1].pixArr = pixArr;
		data[y-1].y = y;
		data[y-1].width = width;
		if(pthread_create(&th[y-1], NULL, &work, (void*)&data[y-1]) != 0)
			return 5;
	}

	//wait while threads complete work
	for(int y = 1; y < height - 1; y++)
		if(pthread_join(th[y-1], NULL) != 0)
			return 6;

	//writing first row
	for(int x = 0; x < width; x++)
		buf[x] = 0;
	write(wfd, buf, width * 3);
	write(wfd, &tmp, padding);

	//writing processed pixels
	for(int y = 1; y < height - 1; y++) {
		write(wfd, &tmp, 3);	//write first pixel in row
		for(int x = 1; x < width - 1; x++) {
			buf[(x-1)*3] = (sobelArr[y * width + x] >> 16) & 255;
			buf[(x-1)*3+1] = (sobelArr[y * width + x] >> 8) & 255;
			buf[(x-1)*3+2] = (sobelArr[y * width + x] ) & 255;
		}
		write(wfd, buf, (width - 2) * 3);	//writing processed row
		write(wfd, &tmp, 3);				//writing last pixel in row
		write(wfd, &tmp, padding);			//writing padding
	}

	//writing last row of image
	for(int x = 0; x < width; x++)
		buf[x] = 0;

	write(wfd, buf, width * 3);
	write(wfd, &tmp, padding);

	close(wfd);

	free(th);
	free(data);
	free(pixArr);
	free(sobelArr);

	exit(0);
}
