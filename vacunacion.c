#include<stdio.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<stdlib.h>
#include<fcntl.h>
#include<string.h>
#include<sys/stat.h>
#include<pthread.h>
#include<math.h>

typedef struct {
	int poblacion;
	int tanda; // pacientes por tanda
	int vacunas_iniciales;
	int objetivo; // vacunas que debe fabricar cada fabrica
	int min_vacunas;
	int max_vacunas;
	int min_fabricacion;
	int max_fabricacion;
	int max_reparto;
	int max_reaccion;
	int max_desplazamiento;
} TEntrada;

typedef struct {
	int stock;
	int demanda;
	int vacunas_recibidas;
	int habitantes_vacunados;
} TCentroVacunacion;

pthread_mutex_t centro[5];
pthread_mutex_t escritura;
pthread_cond_t disponibilidad[5];
TEntrada entrada;
TCentroVacunacion centro_vacunacion[5];
int entrega[3][5]; // vacunas que la fabrica i entrega al centro j para la estadistica final
int terminado;
FILE *salida;
FILE *entra;

void *fabricar(void *num) { // hay que hacer un while hasta que se fabriquen objetivo
	int i;
	int fab;
	int total;
	int fabricadas;
	int reparto[5];
	int demanda[5];
	fab = *(int *)num;
	total = 0;
	while(total<entrada.objetivo) {
		// la fabrica va a preparar un numero aleatorio de vacunas
		fabricadas = rand() % (entrada.max_vacunas-entrada.min_vacunas+1) + entrada.min_vacunas;
		if(total+fabricadas>entrada.objetivo) { // si supera el objetivo solo se producen las que faltan
			fabricadas = entrada.objetivo-total;
			total = entrada.objetivo;
		}
		else {
			total = total + fabricadas;
		}
		pthread_mutex_lock(&escritura);
		printf("Fábrica %d prepara %d vacunas\n", fab, fabricadas);
		fprintf(salida, "Fábrica %d prepara %d vacunas\n", fab, fabricadas);
		pthread_mutex_unlock(&escritura);
		// la fabrica tarda un tiempo aleatorio en fabricar las vacunas
		sleep(rand() % (entrada.max_fabricacion-entrada.min_fabricacion+1) + entrada.min_fabricacion);
		// la fabrica tarda un tiempo aleatorio en repartir las vacunas
		sleep(rand() % entrada.max_reparto + 1);
		// ¿como se hace el reparto?
		// primero cubrimos la demanda de cada centro
		for(i=0; i<5; i++) {
			demanda[i] = centro_vacunacion[i].demanda;
		}
		for(i=0; i<5; i++) {
			if(fabricadas>=demanda[i] && demanda[i]>=0) {
				reparto[i] = demanda[i];
				fabricadas = fabricadas - demanda[i];
			}
			else if(demanda[i]>=0) {
				reparto[i] = fabricadas;
				fabricadas = 0;
			}
			else {
				reparto[i] = 0;
			}
		}
		// si cubrimos la demanda de todos los centros y sobran, se reparten las restantes
		if(fabricadas>0) {
			for(i=0; i<5; i++) {
				reparto[i] = reparto[i] + fabricadas/5;
				if(i==0 && fabricadas%5>0) {
					reparto[i]++;
				}
				if(i==1 && fabricadas%5>1) {
					reparto[i]++;
				}
				if(i==2 && fabricadas%5>2) {
					reparto[i]++;
				}
				if(i==3 && fabricadas%5>3) {
					reparto[i]++;
				}
			}
			fabricadas = 0;
		}
		// realizamos el reparto
		for(i=0; i<5; i++) {
			entrega[fab][i] = entrega[fab][i] + reparto[i];
			// seccion critica
			pthread_mutex_lock(&centro[i]);
			pthread_mutex_lock(&escritura);
			printf("Fábrica %d entrega %d vacunas en el centro %d\n", fab, reparto[i], i);
			fprintf(salida, "Fábrica %d entrega %d vacunas en el centro %d\n", fab, reparto[i], i);
			pthread_mutex_unlock(&escritura);
			centro_vacunacion[i].stock = centro_vacunacion[i].stock + reparto[i];
			centro_vacunacion[i].demanda = centro_vacunacion[i].demanda - reparto[i];
			centro_vacunacion[i].vacunas_recibidas = centro_vacunacion[i].vacunas_recibidas + reparto[i];
			if(reparto[i]>0) {
				pthread_cond_broadcast(&disponibilidad[i]);
			}
			pthread_mutex_unlock(&centro[i]);
		}
	}
	// la fabrica ha fabricado todas las vacunas que le corresponden
	pthread_mutex_lock(&escritura);
	printf("Fábrica %d ha fabricado todas sus vacunas\n", fab);
	fprintf(salida, "Fábrica %d ha fabricado todas sus vacunas\n", fab);
	pthread_mutex_unlock(&escritura);
	// las fabricas van a seguir trabajando (solo 1). van a trasladar las vacunas de los centros que no las necesiten (demanda negativa) a las que si las necesitan
	if(fab==0) {
		while(!terminado) {
			sleep(rand() % entrada.max_reparto + 1);
			fabricadas = 0;
			for(i=0; i<5; i++) {
				pthread_mutex_lock(&centro[i]);
				demanda[i] = centro_vacunacion[i].demanda;
				if(demanda[i]<0) {
					centro_vacunacion[i].stock = centro_vacunacion[i].stock + demanda[i];
					centro_vacunacion[i].demanda = 0;
					fabricadas = fabricadas - demanda[i];
				}
				pthread_mutex_unlock(&centro[i]);
			}
			for(i=0; i<5; i++) {
				if(fabricadas>=demanda[i] && demanda[i]>=0) {
					reparto[i] = demanda[i];
					fabricadas = fabricadas - demanda[i];
				}
				else if(demanda[i]>=0) {
					reparto[i] = fabricadas;
					fabricadas = 0;
				}
				else {
					reparto[i] = 0;
				}
			}
			// si cubrimos la demanda de todos los centros y sobran, se reparten las restantes
			if(fabricadas>0) {
				for(i=0; i<5; i++) {
					reparto[i] = reparto[i] + fabricadas/5;
					if(i==0 && fabricadas%5>0) {
						reparto[i]++;
					}
					if(i==1 && fabricadas%5>1) {
						reparto[i]++;
					}
					if(i==2 && fabricadas%5>2) {
						reparto[i]++;
					}
					if(i==3 && fabricadas%5>3) {
						reparto[i]++;
					}
				}
				fabricadas = 0;
			}
			// realizamos el reparto
			for(i=0; i<5; i++) {
				// seccion critica
				pthread_mutex_lock(&centro[i]);
				centro_vacunacion[i].stock = centro_vacunacion[i].stock + reparto[i];
				centro_vacunacion[i].demanda = centro_vacunacion[i].demanda - reparto[i];
				if(reparto[i]>0) {
					pthread_cond_broadcast(&disponibilidad[i]);
				}
				pthread_mutex_unlock(&centro[i]);
			}
		}
	}
	else {
		while(!terminado);
	}
	pthread_exit(NULL);
}

void *vacunar(void *num) {
	int persona;
	int aleatorio;
	persona = *(int *)num;
	// el paciente tarda un tiempo aleatorio en reaccionar a la cita de vacunación
	sleep(rand() % entrada.max_reaccion + 1);
	// el paciente selecciona uno de los 5 centros para vacunarse (de manera aleatoria)
	aleatorio = rand() % 5 + 1;
	aleatorio--;
	pthread_mutex_lock(&escritura);
	printf("Habitante %d ha elegido el centro de vacunación %d\n", persona, aleatorio);
	fprintf(salida, "Habitante %d ha elegido el centro de vacunación %d\n", persona, aleatorio);
	pthread_mutex_unlock(&escritura);
	// el paciente tarda un tiempo aleatorio en desplazarse hasta el centro
	sleep(rand() % entrada.max_desplazamiento + 1);
	// seccion critica. Si hay vacunas se vacuna, si no hay espera a que haya vacunas (condicion)
	pthread_mutex_lock(&centro[aleatorio]);
	centro_vacunacion[aleatorio].demanda++;
	while(centro_vacunacion[aleatorio].stock<=0) {
		pthread_cond_wait(&disponibilidad[aleatorio], &centro[aleatorio]);
	}
	centro_vacunacion[aleatorio].stock--;
	centro_vacunacion[aleatorio].habitantes_vacunados++;
	pthread_mutex_unlock(&centro[aleatorio]);
	// el paciente ha sido vacunado
	pthread_mutex_lock(&escritura);
	printf("Habitante %d ha sido vacunado en el centro %d\n", persona, aleatorio);
	fprintf(salida, "Habitante %d ha sido vacunado en el centro %d\n", persona, aleatorio);
	pthread_mutex_unlock(&escritura);
	pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
	int i, j, k;
	int fabrica[3];
	int *habitante;
	int restantes; // por si la poblacion no es divisible entre 10
	int habitante_extra;
	pthread_t hilo_fabrica[3];
	pthread_t *hilo_habitante;
	pthread_t hilo_habitante_extra;
	terminado = 0;
	if(argc==1) {
		entra = fopen("entrada_vacunacion.txt", "r");
		if(entra==NULL) {
			fprintf(stderr, "Error. el fichero entrada_vacunacion.txt no existe\n");
			return 1;
		}
		salida = fopen("salida_vacunacion.txt", "w");
		if(salida==NULL) {
			fprintf(stderr, "Error. el fichero salida_vacunacion.txt no existe\n");
			return 1;
		}
	}
	else if(argc==2) {
		entra = fopen(argv[1], "r");
		if(entra==NULL) {
			fprintf(stderr, "Error. el fichero %s no existe\n", argv[1]);
			return 1;
		}
		salida = fopen("salida_vacunacion.txt", "w");
		if(salida==NULL) {
			fprintf(stderr, "Error. el fichero salida_vacunacion.txt no existe\n");
			return 1;
		}
	}
	else if(argc==3) {
		entra = fopen(argv[1], "r");
		if(entra==NULL) {
			fprintf(stderr, "Error. el fichero %s no existe\n", argv[1]);
			return 1;
		}
		salida = fopen(argv[2], "w");
		if(salida==NULL) {
			fprintf(stderr, "Error. el fichero %s no existe\n", argv[2]);
			return 1;
		}
	}
	else {
		fprintf(stderr, "Error. demasiados argumentos\n");
		exit(1);
	}
	printf("VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");
	fprintf(salida, "VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");
	fscanf(entra, "%d", &entrada.poblacion);
	printf("Habitantes: %d\n", entrada.poblacion);
	fprintf(salida, "Habitantes: %d\n", entrada.poblacion);
	printf("Centros de vacunación: %d\n", 5);
	fprintf(salida, "Centros de vacunación: %d\n", 5);
	printf("Fábricas: %d\n", 3);
	fprintf(salida, "Fábricas: %d\n", 3);
	entrada.tanda = entrada.poblacion/10;
	printf("Vacunados por tanda: %d\n", entrada.tanda);
	fprintf(salida, "Vacunados por tanda: %d\n", entrada.tanda);
	fscanf(entra, "%d", &entrada.vacunas_iniciales);
	printf("Vacunas iniciales en cada centro: %d\n", entrada.vacunas_iniciales);
	fprintf(salida, "Vacunas iniciales en cada centro: %d\n", entrada.vacunas_iniciales);
	if(entrada.poblacion%3!=0) {
		entrada.objetivo = entrada.poblacion/3 + 1;
	}
	else {
		entrada.objetivo = entrada.poblacion/3;
	}
	
	// leemos todos los datos
	printf("Vacunas totales por fábrica: %d\n", entrada.objetivo);
	fprintf(salida, "Vacunas totales por fábrica: %d\n", entrada.objetivo);
	fscanf(entra, "%d", &entrada.min_vacunas);
	printf("Mínimo número de vacunas fabricadas en cada tanda: %d\n", entrada.min_vacunas);
	fprintf(salida, "Mínimo número de vacunas fabricadas en cada tanda: %d\n", entrada.min_vacunas);
	fscanf(entra, "%d", &entrada.max_vacunas);
	printf("Máximo número de vacunas fabricadas en cada tanda: %d\n", entrada.max_vacunas);
	fprintf(salida, "Máximo número de vacunas fabricadas en cada tanda: %d\n", entrada.max_vacunas);
	fscanf(entra, "%d", &entrada.min_fabricacion);
	printf("Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", entrada.min_fabricacion);
	fprintf(salida, "Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", entrada.min_fabricacion);
	fscanf(entra, "%d", &entrada.max_fabricacion);
	printf("Tiempo máximo de fabricación de una tanda de vacunas: %d\n", entrada.max_fabricacion);
	fprintf(salida, "Tiempo máximo de fabricación de una tanda de vacunas: %d\n", entrada.max_fabricacion);
	fscanf(entra, "%d", &entrada.max_reparto);
	printf("Tiempo máximo de reparto de vacunas a los centros: %d\n", entrada.max_reparto);
	fprintf(salida, "Tiempo máximo de reparto de vacunas a los centros: %d\n", entrada.max_reparto);
	fscanf(entra, "%d", &entrada.max_reaccion);
	printf("Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", entrada.max_reaccion);
	fprintf(salida, "Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", entrada.max_reaccion);
	fscanf(entra, "%d", &entrada.max_desplazamiento);
	printf("Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", entrada.max_desplazamiento);
	fprintf(salida, "Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", entrada.max_desplazamiento);
	fclose(entra);
	// ya se han leido todos los datos
	
	printf("\n");
	fprintf(salida, "\n");
	printf("PROCESO DE VACUNACIÓN\n");
	fprintf(salida, "PROCESO DE VACUNACIÓN\n");
	srand(time(NULL));
	habitante = malloc(entrada.tanda*sizeof(int));
	hilo_habitante = malloc(entrada.tanda*sizeof(pthread_t));
	for(i=0; i<5; i++) { // un mutex por centro de vacunacion (recurso compartido por fábricas y habitantes)
		centro_vacunacion[i].stock=entrada.vacunas_iniciales;
		centro_vacunacion[i].demanda = -entrada.vacunas_iniciales;
		centro_vacunacion[i].vacunas_recibidas=0;
		centro_vacunacion[i].habitantes_vacunados=0;
		pthread_mutex_init(&centro[i], NULL);
		pthread_cond_init(&disponibilidad[i], NULL);
	}
	for(i=0; i<3; i++) { // un hilo de ejecución por fábrica
		fabrica[i] = i;
		if(pthread_create(&hilo_fabrica[i], NULL, fabricar, (void*) &fabrica[i])!=0) {
			fprintf(stderr, "Error al crear el thread\n");
			exit(1);
		}
	}
	k=0;
	restantes = entrada.poblacion%10 - 1;
	for(i=0; i<10; i++) {
		for(j=0; j<entrada.tanda; j++) { // un hilo de ejecución por habitante
			habitante[j] = k;
			if(pthread_create(&hilo_habitante[j], NULL, vacunar, (void*) &habitante[j])!=0){
				fprintf(stderr, "Error al crear el thread\n");
				exit(1);
			}
			k++;
		}
		if(i<=restantes) {
			habitante_extra = k;
			if(pthread_create(&hilo_habitante_extra, NULL, vacunar, (void*) &habitante_extra)!=0){
				fprintf(stderr, "Error al crear el thread\n");
				exit(1);
			}
			k++;
		}
		for(j=0; j<entrada.tanda; j++) {
			pthread_join(hilo_habitante[j], NULL);
		}
		if(i<=restantes) {
			pthread_join(hilo_habitante_extra, NULL);
		}
	}
	terminado=1;
	for(i=0; i<3; i++) {
		pthread_join(hilo_fabrica[i], NULL);
	}
	for(i=0; i<5; i++) {
		pthread_mutex_destroy(&centro[i]);
		pthread_cond_destroy(&disponibilidad[i]);
	}
	free(habitante);
	free(hilo_habitante);
	printf("Vacunación finalizada\n\n");
	fprintf(salida, "Vacunación finalizada\n\n");
	// estadistica final
	//estadistica fabricas
	for(i=0; i<3; i++) {
		printf("FÁBRICA %d\n", i);
		fprintf(salida, "FÁBRICA %d\n", i);
		printf("Vacunas fabricadas: %d\n", entrada.objetivo);
		fprintf(salida, "Vacunas fabricadas: %d\n", entrada.objetivo);
		for(j=0; j<5; j++) {
			printf("Vacunas entregadas al centro de vacunación %d: %d\n", j, entrega[i][j]);
			fprintf(salida, "Vacunas entregadas al centro de vacunación %d: %d\n", j, entrega[i][j]);
		}
		printf("\n");
		fprintf(salida, "\n");
	}
	//estadistica centros
	for(i=0; i<5; i++) {
		printf("CENTRO DE VACUNACIÓN %d\n", i);
		fprintf(salida, "CENTRO DE VACUNACIÓN %d\n", i);
		printf("Vacunas recibidas: %d\n", centro_vacunacion[i].vacunas_recibidas);
		fprintf(salida, "Vacunas recibidas: %d\n", centro_vacunacion[i].vacunas_recibidas);
		printf("Habitantes vacunados: %d\n", centro_vacunacion[i].habitantes_vacunados);
		fprintf(salida, "Habitantes vacunados: %d\n", centro_vacunacion[i].habitantes_vacunados);
		printf("Vacunas que han sobrado: %d\n\n", centro_vacunacion[i].stock);
		fprintf(salida, "Vacunas que han sobrado: %d\n\n", centro_vacunacion[i].stock);
	}
	fclose(salida);
	exit(0);
}
