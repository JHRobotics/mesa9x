#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*
 Based on example from here:
 from here http://www.nirsoft.net/vc/change_screen_brightness.html
*/

BOOL SetGamma(HDC hDC, double gamma)
{
	BOOL bReturn = FALSE;
	HDC hGammaDC = hDC;

	//Load the display device context of the entire screen if hDC is NULL.
	if (hDC == NULL)
		hGammaDC = GetDC(NULL);

	if (hGammaDC != NULL)
	{
		//Generate the 256-colors array for the specified wBrightness value.
		WORD GammaArray[3][256];

		for (int iIndex = 0; iIndex < 256; iIndex++)
		{
			double base_chroma = iIndex/255.0f; /* conver color to <0.0, 1.0> range */
			double new_chroma  = 0.0f;
			if(gamma > 0.0f)
			{
				new_chroma = pow(base_chroma, 1.0/gamma);
			}
			
			if(new_chroma < 0.0f)
				new_chroma = 0.0f;
			else if(new_chroma > 1.0f)
				new_chroma = 1.0f;

			GammaArray[0][iIndex] = 
			GammaArray[1][iIndex] = 
			GammaArray[2][iIndex] = (WORD)(new_chroma * 255.0f * 256.0f);
			/* ^ we can look on this value as on u8.8 fixed point number. 
			     But only integer values are significant for most video drivers
			 */
		}

		//Set the GammaArray values into the display device context.
		bReturn = SetDeviceGammaRamp(hGammaDC, GammaArray);
	}
	else
	{
		printf("DC NULL\n");
	}

	if (hDC == NULL)
		ReleaseDC(NULL, hGammaDC);

	return bReturn;
}

int main(int argc, char *argv[])
{
	if(argc < 2)
	{
		printf("%s <gamma>\n1.0 is default, 0.0 is minimum (all black)\n", argv[0]);
	}
	else
	{
		double gamma = atof(argv[1]);
		BOOL rc = SetGamma(NULL, gamma);
		if(rc)
		{
			printf("SUCCESS\n");
		}
		else
		{
			printf("FAILED\n");
			return EXIT_FAILURE;
		}
	}
	
	return EXIT_SUCCESS;
}
