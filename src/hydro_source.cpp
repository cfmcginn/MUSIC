// Copyright 2017 Chun Shen

#include <omp.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "./hydro_source.h"
#include "./util.h"

using namespace std;

hydro_source::hydro_source(InitData *DATA_in) {
    DATA_ptr = DATA_in;
    source_tau_max = 0.0;
    if (DATA_ptr->Initial_profile == 12) {  // MC-Glauber-LEXUS
        sigma_tau = 0.1;
        sigma_x = 0.5;
        sigma_eta = 0.5;
        volume = DATA_ptr->delta_x*DATA_ptr->delta_y*DATA_ptr->delta_eta;
        string_dump_mode = DATA_ptr->string_dump_mode;
        read_in_QCD_strings_and_partons();
    }
    if (DATA_ptr->Initial_profile == 30) {  // AMPT
        sigma_tau = 0.1;
        sigma_x = 0.5;
        sigma_eta = 0.5;
        volume = DATA_ptr->delta_x*DATA_ptr->delta_y*DATA_ptr->delta_eta;
        read_in_AMPT_partons();
    }
}

hydro_source::~hydro_source() {
    if (DATA_ptr->Initial_profile == 12) {
        QCD_strings_list.clear();
        parton_list.clear();
    }
    if (DATA_ptr->Initial_profile == 30) {
        parton_list.clear();
    }
}

//! This function reads in the spatal information of the strings and partons
//! which are produced from the MC-Glauber-LEXUS model
void hydro_source::read_in_QCD_strings_and_partons() {
    string QCD_strings_filename = DATA_ptr->initName;
    string partons_filename = DATA_ptr->initName_rhob;
    cout << "read in QCD strings list from " << QCD_strings_filename
         << " and partons list from " << partons_filename << endl;
    string text_string;

    ifstream QCD_strings_file(QCD_strings_filename.c_str());
    if (!QCD_strings_file) {
        cerr << "Error:hydro_source::read_in_QCD_strings_and_partons: "
             << "can not open QCD strings file: " << QCD_strings_filename
             << endl;
        exit(1);
    }
    getline(QCD_strings_file, text_string);  // read the header
    // now we read in data
    getline(QCD_strings_file, text_string);
    while (!QCD_strings_file.eof()) {
        stringstream text_stream(text_string);
        QCD_string new_string;
        text_stream >> new_string.norm >> new_string.delta_E
                    >> new_string.tau_form
                    >> new_string.x_perp >> new_string.y_perp
                    >> new_string.eta_s_left >> new_string.eta_s_right
                    >> new_string.y_l >> new_string.y_r;
        QCD_strings_list.push_back(new_string);
        if (source_tau_max < new_string.tau_form) {
            source_tau_max = new_string.tau_form;
        }
        getline(QCD_strings_file, text_string);
    }
    QCD_strings_file.close();
    cout << "hydro_source: tau_max = " << source_tau_max << " fm." << endl;
    
    ifstream partons_file(partons_filename.c_str());
    if (!partons_file) {
        cerr << "Error:hydro_source::read_in_QCD_strings_and_partons: "
             << "can not open parton list file: " << partons_filename
             << endl;
        exit(1);
    }
    getline(partons_file, text_string);      // read the header
    // now we read in data
    getline(partons_file, text_string);
    while (!partons_file.eof()) {
        stringstream text_stream(text_string);
        parton new_parton;
        text_stream >> new_parton.tau >> new_parton.x >> new_parton.y
                    >> new_parton.eta_s >> new_parton.rapidity;
        new_parton.baryon_number = 1.0;
        parton_list.push_back(new_parton);
        getline(partons_file, text_string);
    }
    partons_file.close();
}


//! This function reads in the partons information from the AMPT model
void hydro_source::read_in_AMPT_partons() {
    string AMPT_filename = DATA_ptr->initName_AMPT;
    cout << "[Info] hydro_source: "
         << "read in AMPT parton list from " << AMPT_filename << endl;

    string text_string;
    ifstream AMPT_file(AMPT_filename.c_str());
    if (!AMPT_file) {
        cerr << "Error:hydro_source::read_in_AMPT_partons: "
             << "can not open the AMPT file: " << AMPT_filename
             << endl;
        exit(1);
    }

    int n_partons = 0;
    AMPT_file >> n_partons;

    // now we read in data
    getline(AMPT_file, text_string);
    while (!AMPT_file.eof()) {
        stringstream text_stream(text_string);
        parton new_parton;
        double t_local, z_local, pz_local;
        text_stream >> t_local >> new_parton.x >> new_parton.y >> z_local
                    >> new_parton.E >> new_parton.px >> new_parton.py
                    >> pz_local;
        if (t_local > z_local) {
            // the parton is inside the light cone
            double mass_sq = (
                new_parton.E*new_parton.E - new_parton.px*new_parton.px
                - new_parton.py*new_parton.py - pz_local*pz_local);
            if (mass_sq > 0.) {
                new_parton.mass = sqrt(mass_sq);
                new_parton.tau = sqrt(t_local*t_local - z_local*z_local);
                new_parton.eta_s = 0.5*log((t_local + z_local)
                                           /(t_local - z_local + 1e-15));
                new_parton.rapidity = 0.5*log((new_parton.E + pz_local)
                                              /(new_parton.E - pz_local));
                parton_list.push_back(new_parton);
                if (source_tau_max < new_parton.tau) {
                    source_tau_max = new_parton.tau;
                }
            }
        }
        getline(AMPT_file, text_string);
    }
    AMPT_file.close();
    cout << "[Info] hydro_source:: read in " << parton_list.size() << "/"
         << n_partons << " partons." << endl;
    cout << "[Info] hydro_source:: tau_max = " << source_tau_max << " fm."
         << endl;
}

void hydro_source::get_hydro_energy_source(
    double tau, double x, double y, double eta_s, double *u_mu, double *j_mu) {
    // clean up j_mu
    for (int i = 0; i < 4; i++) {
        j_mu[i] = 0.0;
    }
    if (DATA_ptr->Initial_profile == 12) {
        // double ed = 0.;
        double n_sigma_skip = 5.;
        double tau_dis_max = tau - source_tau_max;
        if (tau_dis_max < n_sigma_skip*sigma_tau) {
            double prefactor_prep = 1./(M_PI*sigma_x*sigma_x);
            double prefactor_tau = 1./(sqrt(M_PI)*sigma_tau);
            for (vector<QCD_string>::iterator it = QCD_strings_list.begin();
                 it != QCD_strings_list.end(); it++) {
                // skip the evaluation if the strings is too far away in the
                // space-time grid
                double tau_dis = tau - (*it).tau_form;
                if (fabs(tau_dis) > n_sigma_skip*sigma_tau) {
                    continue;
                }
                double x_dis = x - (*it).x_perp;
                if (fabs(x_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double y_dis = y - (*it).y_perp;
                if (fabs(y_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double eta_s_left = (*it).eta_s_left;
                double eta_s_right = (*it).eta_s_right;
                if (string_dump_mode == 2) {
                    eta_s_left = (*it).y_l;
                    eta_s_right = (*it).y_r;
                }
                if (eta_s < eta_s_left - n_sigma_skip*sigma_eta
                     || eta_s > eta_s_right + n_sigma_skip*sigma_eta) {
                    continue;
                }
                double exp_tau = (
                        1./((*it).tau_form)
                        *exp(-tau_dis*tau_dis/(sigma_tau*sigma_tau)));
                double exp_xperp = exp(-(x_dis*x_dis + y_dis*y_dis)
                                        /(sigma_x*sigma_x));
                double exp_eta_s = 1.;
                if (eta_s < eta_s_left) {
                    double eta_s_dis = eta_s - eta_s_left;
                    exp_eta_s = (
                            exp(-(eta_s_dis*eta_s_dis)/(sigma_eta*sigma_eta)));
                }
                if (eta_s > eta_s_right) {
                    double eta_s_dis = eta_s - eta_s_right;
                    exp_eta_s = (
                            exp(-(eta_s_dis*eta_s_dis)/(sigma_eta*sigma_eta)));
                }
                double e_local = exp_tau*exp_xperp*exp_eta_s;
                e_local *= DATA_ptr->sFactor/hbarc;  // 1/fm^4
                double y_interp = (
                        (*it).y_l + ((*it).y_r - (*it).y_l)
                                    /(eta_s_right - eta_s_left)
                                    *(eta_s - eta_s_left));
                j_mu[0] += cosh(y_interp - eta_s)*e_local;
                j_mu[1] += 0.0;
                j_mu[2] += 0.0;
                j_mu[3] += sinh(y_interp - eta_s)*e_local;
                // ed += e_local;
            }
            j_mu[0] *= prefactor_tau*prefactor_prep;
            j_mu[1] *= prefactor_tau*prefactor_prep;
            j_mu[2] *= prefactor_tau*prefactor_prep;
            j_mu[3] *= prefactor_tau*prefactor_prep;
            // ed = ed*DATA_ptr->sFactor/hbarc;   // 1/fm^4
            // j_mu[0] = ed*u_mu[0]*prefactor_tau*prefactor_prep;
            // j_mu[1] = ed*u_mu[1]*prefactor_tau*prefactor_prep;
            // j_mu[2] = ed*u_mu[2]*prefactor_tau*prefactor_prep;
            // j_mu[3] = ed*u_mu[3]*prefactor_tau*prefactor_prep;
        }
    } else if (DATA_ptr->Initial_profile == 30) {
        // AMPT parton sources
        double n_sigma_skip = 5.;
        double tau_dis_max = tau - source_tau_max;
        if (tau_dis_max < n_sigma_skip*sigma_tau) {
            double prefactor_prep = 1./(M_PI*sigma_x*sigma_x);
            double prefactor_tau = 1./(sqrt(M_PI)*sigma_tau);
            double prefactor_etas = 1./(sqrt(M_PI)*sigma_eta);
            for (vector<parton>::iterator it = parton_list.begin();
                 it != parton_list.end(); it++) {
                double tau_dis = tau - (*it).tau;
                if (fabs(tau_dis) > n_sigma_skip*sigma_tau) {
                    continue;
                }
                double x_dis = x - (*it).x;
                if (fabs(x_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double y_dis = y - (*it).y;
                if (fabs(y_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double eta_s_dis = eta_s - (*it).eta_s;
                if (fabs(eta_s_dis) > n_sigma_skip*sigma_eta) {
                    continue;
                }
                double exp_tau = (
                    1./((*it).tau)
                    *exp(-tau_dis*tau_dis/(sigma_tau*sigma_tau)));
                double exp_xperp = exp(-(x_dis*x_dis + y_dis*y_dis)
                                        /(sigma_x*sigma_x));
                double exp_eta_s = (
                        exp(-eta_s_dis*eta_s_dis/(sigma_eta*sigma_eta)));

                double f_smear = exp_tau*exp_xperp*exp_eta_s;
                double p_perp_sq = (*it).px*(*it).px + (*it).py*(*it).py;
                double m_perp = sqrt((*it).mass*(*it).mass + p_perp_sq);
                j_mu[0] += m_perp*cosh((*it).rapidity - eta_s)*f_smear;
                j_mu[1] += (*it).px*f_smear;
                j_mu[2] += (*it).py*f_smear;
                j_mu[3] += m_perp*sinh((*it).rapidity - eta_s)*f_smear;
            }
            double norm = DATA_ptr->sFactor/hbarc;     // 1/fm^4
            j_mu[0] *= norm*prefactor_tau*prefactor_prep*prefactor_etas;
            j_mu[1] *= norm*prefactor_tau*prefactor_prep*prefactor_etas;
            j_mu[2] *= norm*prefactor_tau*prefactor_prep*prefactor_etas;
            j_mu[3] *= norm*prefactor_tau*prefactor_prep*prefactor_etas;
        }
    }
}

double hydro_source::get_hydro_rhob_source(double tau, double x, double y,
                                           double eta_s) {
    double res = 0.;
    if (DATA_ptr->Initial_profile == 12) {
        double n_sigma_skip = 5.;
        double tau_dis_max = tau - source_tau_max;
        if (tau_dis_max < n_sigma_skip*sigma_tau) {
            double prefactor_prep = 1./(M_PI*sigma_x*sigma_x);
            double prefactor_etas = 1./(sqrt(M_PI)*sigma_eta);
            double prefactor_tau = 1./(sqrt(M_PI)*sigma_tau);
            for (vector<parton>::iterator it = parton_list.begin();
                 it != parton_list.end(); it++) {
                // skip the evaluation if the strings is too far away in the
                // space-time grid
                double tau_dis = tau - (*it).tau;
                if (fabs(tau_dis) > n_sigma_skip*sigma_tau) {
                    continue;
                }
                double x_dis = x - (*it).x;
                if (fabs(x_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double y_dis = y - (*it).y;
                if (fabs(y_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double eta_s_0 = (*it).eta_s;
                if (string_dump_mode == 2) {
                    eta_s_0 = (*it).rapidity;
                }
                double eta_s_dis = eta_s - eta_s_0;
                if (fabs(eta_s_dis) > n_sigma_skip*sigma_eta) {
                    continue;
                }
                double exp_tau = (
                    1./((*it).tau)
                    *exp(-tau_dis*tau_dis/(sigma_tau*sigma_tau)));
                double exp_xperp = exp(-(x_dis*x_dis + y_dis*y_dis)
                                        /(sigma_x*sigma_x));
                double exp_eta_s = (
                        exp(-eta_s_dis*eta_s_dis/(sigma_eta*sigma_eta)));
                res += exp_tau*exp_xperp*exp_eta_s;
            }
            res *= prefactor_tau*prefactor_prep*prefactor_etas;
        }
    } else if (DATA_ptr->Initial_profile == 30) {
        double n_sigma_skip = 5.;
        double tau_dis_max = tau - source_tau_max;
        if (tau_dis_max < n_sigma_skip*sigma_tau) {
            double prefactor_prep = 1./(M_PI*sigma_x*sigma_x);
            double prefactor_etas = 1./(sqrt(M_PI)*sigma_eta);
            double prefactor_tau = 1./(sqrt(M_PI)*sigma_tau);
            for (vector<parton>::iterator it = parton_list.begin();
                 it != parton_list.end(); it++) {
                // skip the evaluation if the strings is too far away in the
                // space-time grid
                double tau_dis = tau - (*it).tau;
                if (fabs(tau_dis) > n_sigma_skip*sigma_tau) {
                    continue;
                }
                double x_dis = x - (*it).x;
                if (fabs(x_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double y_dis = y - (*it).y;
                if (fabs(y_dis) > n_sigma_skip*sigma_x) {
                    continue;
                }
                double eta_s_dis = eta_s - (*it).eta_s;
                if (fabs(eta_s_dis) > n_sigma_skip*sigma_eta) {
                    continue;
                }
                double exp_tau = (
                    1./((*it).tau)
                    *exp(-tau_dis*tau_dis/(sigma_tau*sigma_tau)));
                double exp_xperp = exp(-(x_dis*x_dis + y_dis*y_dis)
                                        /(sigma_x*sigma_x));
                double exp_eta_s = (
                        exp(-eta_s_dis*eta_s_dis/(sigma_eta*sigma_eta)));
                double f_smear = exp_tau*exp_xperp*exp_eta_s;
                res += f_smear;

            }
            res *= prefactor_tau*prefactor_prep*prefactor_etas;
        }
    }
    return(res);
}

void hydro_source::get_hydro_energy_source_before_tau(
    double tau, double x, double y, double eta_s, double *j_mu) {
    double *u = new double[4];
    double *j_mu_one_step = new double[4];
    for (int i = 0; i < 4; i++) {
        j_mu[i] = 0.0;  // clean up j_mu
        u[i] = 0.0;
    }
    u[0] = 1.0;
    double tau0 = 0.0;
    double dtau = DATA_ptr->delta_tau;
    int n_tau_steps = static_cast<int>((tau - tau0)/dtau);
    for (int i = 0; i < n_tau_steps; i++) {
        for (int j = 0; j < 4; j++) {
            j_mu_one_step[j] = 0.0; }
        double tau_local = tau0 + (i + 0.5)*dtau;
        get_hydro_energy_source(tau_local, x, y, eta_s, u, j_mu_one_step);
        for (int j = 0; j < 4; j++) {
            j_mu[j] += tau_local*j_mu_one_step[j]*dtau;
        }
    }
    for (int j = 0; j < 4; j++) {
        j_mu[j] /= tau;
    }
    delete[] u;
    delete[] j_mu_one_step;
}

double hydro_source::get_hydro_rhob_source_before_tau(
        double tau, double x, double y, double eta_s) {
    double res = 0.;
    double tau0 = 0.0;
    double dtau = DATA_ptr->delta_tau;
    int n_tau_steps = static_cast<int>((tau - tau0)/dtau);
    for (int i = 0; i < n_tau_steps; i++) {
        double res_local = 0.0;
        double tau_local = tau0 + (i + 0.5)*dtau;
        res_local = get_hydro_rhob_source(tau_local, x, y, eta_s);
        res += tau_local*res_local*dtau;
    }
    res /= tau;
    return(res);
}
