//Модифицированный код
#include "stdafx.h"
#include "omp.h"
#include <iostream>

#include <fstream>
#include "cstdlib"
#include "iomanip"

struct ReImNum
{
	double re;
	double im;
};

//i1 - ведущий элемент, а также номер строки куда нужно поставить строку с максимальным диагональным элементом
//i2 - номер строки от куда нужно взять строку с максимальным диагональным элементом
//size_A - кол-во столбцов в матрице А, size_B - кол-во столбцов в матрице b вида Ах = b
void swap_line(ReImNum** test_matr, int i1, int i2, int size_A, int size_B)
{
	int np = size_A + size_B;
	ReImNum* t = new ReImNum[np];
	for (int j = i1; j < np; j++)
	{
		t[j] = test_matr[0][i1*np + j];
		test_matr[0][i1*np + j] = test_matr[0][i2*np + j];
		test_matr[0][i2*np + j] = t[j];
	}	
	return;
}


//k - номер обнулямой колонки, cur_proc - id текущего процесса, proc- количество процессов
// matrix_size - размер матрицы А вида Ах = b
static int GetStartIndex(int k, int cur_proc, int proc, int matrix_size)
{
	int eq_number = (matrix_size - k) / proc;
	int overflow_eq = (matrix_size - k) % proc;
	if (overflow_eq != 0)
	{
		eq_number++;
	}
	int cur_cor_order = 0;
	if (overflow_eq != 0 && cur_proc > overflow_eq)
	{
		cur_cor_order = cur_proc - overflow_eq;
	}
	int start = k + cur_proc * eq_number - cur_cor_order;
	//учёт того, что первое уравнение в первом процессе всегда базисное
	if (cur_proc == 0)
	{
		start++;
	}
	return start;
}
static int GetEndIndex(int k, int cur_proc, int proc, int matrix_size)
{
	cur_proc++;
	int eq_number = (matrix_size - k) / proc;
	int overflow_eq = (matrix_size - k) % proc;
	if (overflow_eq != 0)
	{
		eq_number++;
	}
	int cur_cor_order = 0;
	if (overflow_eq != 0 && cur_proc > overflow_eq)
	{
		cur_cor_order = cur_proc - overflow_eq;
	}
	int end = k + cur_proc * eq_number - 1 - cur_cor_order;
	return end;
}
//деление a на b
ReImNum divide_ReImNum(ReImNum a, ReImNum b)
{
	ReImNum res;
	res.re = (a.re*b.re + a.im*b.im) / (b.re*b.re + b.im*b.im);
	res.im = (a.im*b.re - a.re*b.im) / (b.re*b.re + b.im*b.im);
	return res;
}
//умножение a на b
ReImNum mult_ReImNum(ReImNum a, ReImNum b)
{
	ReImNum res;
	res.re = a.re*b.re - a.im*b.im;
	res.im = a.im*b.re + a.re*b.im;
	return res;
}


ReImNum add_ReImNum(ReImNum a, ReImNum b)
{
	ReImNum res;
	res.re = a.re + b.re;
	res.im = a.im + b.im;
	return res;
}
double abs_ReImNum(ReImNum a)
{
	return sqrt(a.re*a.re + a.im* a.im);
}

//C# передаёт двумерный массив в одномерном виде. Преобразование индексов [i][j] -> [0][i*np+j]
extern "C" __declspec(dllexport) 
//Предполагается, что размер матрицы А = matrix_size, а правой части 1
ReImNum* gauss_complex_p_m(ReImNum** A, int matrix_size, int b_count, int &pthread)
{		
    #ifndef _OPENMP
	   std::cout << "OpenMP is not supported!\n";
	   return nullptr;
    #endif

	   const double eps = 0.000001;
	
	   ReImNum* solution = new ReImNum[matrix_size];
	   // Количество колонок с правой частью
	   int np = matrix_size + b_count;
	   //установка числа потоков
	   if (omp_get_max_threads() < pthread)
		   pthread = omp_get_max_threads();

	   omp_set_num_threads(pthread);

	   std::ofstream fout;		    

#pragma omp parallel shared (A, np)
	{
		int proc = omp_get_num_threads();		// Количество потоков
		int cur_proc = omp_get_thread_num();	// Идентификация текущего потока

		// у каждого процесса свои начальные и конечные номера строк
		int start = 0;
		int end = 0;		
		//индекс k пробегает по рабочим столбцам, значение в которых нужно обнулить
		for (int k = 0; k < matrix_size; k++)
		{
			// Поиск строки с максимальным элементом a[i][k] вдоль рабочего столбца			
#pragma omp single
			{
				double max = abs_ReImNum(A[0][k*np + k]);

				//номер уравнения с максимальным диагональным элементом
				int index_max = k;
				for (int i = k + 1; i < matrix_size; i++)
				{
					double val = abs_ReImNum(A[0][i*np + k]);
					if (val > max)
					{
						max = val;
						index_max = i;
					}
				}
				
				if (max < eps)
				{	
					for (int x = 0; x < matrix_size; x++)
					{
						solution[x].re = 0;
						solution[x].im = 0;
						A[0][x * np + matrix_size + b_count].re = 0;
						A[0][x * np + matrix_size + b_count].im = 0;
					}
					k = np;					
				}

				//Перестановка уравнения с наибольшим диагональным элементом                
				if (k != index_max)    //не перреставлять само себя
				{
					swap_line(A, k, index_max, matrix_size, b_count);					
				}
			} //конец работы в мастер потоке

			//точка синхронизации процессов, чтобы получить данные от мастера
#pragma omp barrier
			///////////////////

			start = GetStartIndex(k, cur_proc, proc, matrix_size);
			end = GetEndIndex(k, cur_proc, proc, matrix_size);
			
			ReImNum coef;
			//цикл по строкам системы. Обрабатываются своим процессом.
			for (int i = start; i <= end; i++)
			{
				if (abs_ReImNum(A[0][i*np + k]) < eps)
				{
					A[0][i*np + k].re = 0;
					A[0][i*np + k].im = 0;
					continue; // для нулевого коэффициента пропустить
				}
				coef = divide_ReImNum(A[0][i*np + k], A[0][k*np + k]);
				coef.re = (-1) * coef.re;
				coef.im = (-1) * coef.im;

				for (int j = k; j <= matrix_size; j++)
				{
					if (j == k)
					{
						A[0][i*np + k].re = 0;
						A[0][i*np + k].im = 0;
					}
					else
					{
						A[0][i*np + j] = add_ReImNum(A[0][i*np + j], mult_ReImNum(coef, A[0][k*np + j]));
					}
				}				
			}
			//точка синхронизации процессов, чтобы на след итер не переписался vec, который ещё используется на текущей
#pragma omp barrier		
			/*if (k == 0)
			{
				fout.open("out_data.txt");
				for (int i = 0; i < matrix_size; i++)
				{
					for (int j = 0; j < matrix_size; j++)
					{
						fout << std::setprecision(2) << A[0][i*np + j].re << "\t";
					}
					fout << "\n";
				}
				fout.close();
			}*/
			
		}
	}

	

	//обратный ход с учётом b_count = 1
	ReImNum s;
	for (int k = matrix_size - 1; k >= 0; k--)
	{		
		solution[k] = divide_ReImNum(A[0][k*np + matrix_size], A[0][k*np + k]);

		s.re = 0;
		s.im = 0;
		for (int i = k - 1; i >= 0; i--)
		{
			s = mult_ReImNum(A[0][i*np + k], solution[k]);
			s.re = (-1) * s.re;
			s.im = (-1) * s.im;
			A[0][i * np + matrix_size] = add_ReImNum(A[0][i * np + matrix_size], s);
		}
	}		
	return solution;
}